// SPDX-License-Identifier: GPL-2.0
/*
 * cfg80211_logic_selftest.c - device-free host test for the portable
 * cfg80211 VAP model, registration gate, op/message map and status
 * formatting of the Hi5622V100 clean-room driver (wifi-cfg80211 lane,
 * task-104).  No kernel, no device, no vendor material.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_cfg80211_logic.h"

static int g_checks;
static int g_failed;

#define CHECK(cond)								\
	do {									\
		g_checks++;							\
		if (!(cond)) {							\
			g_failed++;						\
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
		}								\
	} while (0)

#define CHECK_STREQ(a, b)							\
	do {									\
		const char *_a = (a);						\
		const char *_b = (b);						\
		g_checks++;							\
		if (!_a || !_b || strcmp(_a, _b)) {				\
			g_failed++;						\
			printf("FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__, \
			       _a ? _a : "(null)", _b ? _b : "(null)");		\
		}								\
	} while (0)

static void test_model_basics(void)
{
	struct hi5622_cfg_model m;

	hi5622_cfg_model_init(&m, HI5622_BAND_2GHZ);
	CHECK(m.band == HI5622_BAND_2GHZ);
	CHECK(!m.wiphy_allocated);
	CHECK(!m.wiphy_registered);
	CHECK(hi5622_cfg_vap_count(&m) == 0);
	CHECK(hi5622_cfg_vap_get(&m, 0) == NULL);
	CHECK(hi5622_cfg_vap_find_name(&m, "wlan0") == NULL);

	CHECK(hi5622_cfg_iftype_supported(HI5622_IFTYPE_AP));
	CHECK(hi5622_cfg_iftype_supported(HI5622_IFTYPE_STATION));
	CHECK(hi5622_cfg_iftype_supported(HI5622_IFTYPE_MONITOR));
	CHECK(!hi5622_cfg_iftype_supported(HI5622_IFTYPE_AP_VLAN));
	CHECK(!hi5622_cfg_iftype_supported(HI5622_IFTYPE_P2P_CLIENT));
	CHECK_STREQ(hi5622_cfg_iftype_name(HI5622_IFTYPE_AP), "AP");
	CHECK_STREQ(hi5622_cfg_iftype_name(HI5622_IFTYPE_STATION), "managed");
	CHECK_STREQ(hi5622_cfg_iftype_name(HI5622_IFTYPE_MONITOR), "monitor");
	CHECK_STREQ(hi5622_cfg_iftype_name(99), "unknown");
	hi5622_cfg_model_deinit(&m);
}

static void test_vap_lifecycle(void)
{
	struct hi5622_cfg_model m;
	struct hi5622_vap_model *v;
	u16 slot = 0xffff, slot2 = 0xffff, i;
	char name[32];

	hi5622_cfg_model_init(&m, HI5622_BAND_2GHZ);
	CHECK(hi5622_cfg_vap_alloc(&m, "wlan0", HI5622_IFTYPE_AP, &slot) ==
	      -ENODEV);

	m.wiphy_allocated = true;
	m.wiphy_registered = true;
	CHECK(hi5622_cfg_vap_alloc(&m, "wlan0", HI5622_IFTYPE_AP, &slot) == 0);
	CHECK(slot == 0);
	CHECK(hi5622_cfg_vap_count(&m) == 1);
	CHECK(m.netdev_created == 1);
	CHECK(m.hcc_vap_msg_deferred == 1);
	v = hi5622_cfg_vap_get(&m, slot);
	CHECK(v != NULL);
	CHECK(v->state == HI5622_VAP_ALLOCATED);
	CHECK(v->iftype == HI5622_IFTYPE_AP);
	CHECK(v->hcc_add_pending);
	CHECK(v->hcc_del_pending == false);
	CHECK(!strcmp(v->name, "wlan0"));
	CHECK(v->addr[0] == 0x02);		/* locally administered */
	CHECK((v->addr[0] & 1) == 0);		/* unicast */
	CHECK(v->addr[5] == 0);
	CHECK(hi5622_cfg_vap_find_name(&m, "wlan0") == v);
	CHECK(hi5622_cfg_vap_find_name(&m, "wlan1") == NULL);

	CHECK(hi5622_cfg_vap_alloc(&m, "wlan0", HI5622_IFTYPE_AP, &slot2) ==
	      -EEXIST);
	CHECK(hi5622_cfg_vap_alloc(&m, "wlan1", HI5622_IFTYPE_AP_VLAN,
				   &slot2) == -EOPNOTSUPP);
	CHECK(hi5622_cfg_vap_alloc(&m, "", HI5622_IFTYPE_AP, &slot2) == -EINVAL);
	CHECK(hi5622_cfg_vap_alloc(&m, "0123456789abcdef", HI5622_IFTYPE_AP,
				   &slot2) == -EINVAL);
	CHECK(hi5622_cfg_vap_alloc(&m, "wlan1", HI5622_IFTYPE_STATION,
				   &slot2) == 0);
	CHECK(slot2 == 1);
	CHECK(hi5622_cfg_vap_get(&m, slot2)->addr[5] == 1);

	CHECK(hi5622_cfg_vap_mark_registered(&m, slot) == 0);
	CHECK(hi5622_cfg_vap_get(&m, slot)->state == HI5622_VAP_REGISTERED);
	CHECK(hi5622_cfg_vap_mark_registered(&m, slot) == -EINVAL);
	hi5622_cfg_vap_drop(&m, slot);
	CHECK(hi5622_cfg_vap_get(&m, slot) == NULL);
	CHECK(hi5622_cfg_vap_count(&m) == 1);
	CHECK(m.netdev_removed == 1);
	CHECK(hi5622_cfg_vap_alloc(&m, "wlan0", HI5622_IFTYPE_AP, &slot) == 0);
	CHECK(slot == 0);
	hi5622_cfg_vap_drop(&m, 555);	/* out of range: no-op */
	hi5622_cfg_vap_drop(&m, 3);	/* already free: no-op */

	/* fill the table and check -ENOSPC */
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		snprintf(name, sizeof(name), "wlan%u", (unsigned)i);
		slot = 0xffff;
		if (hi5622_cfg_vap_find_name(&m, name))
			continue;
		CHECK(hi5622_cfg_vap_alloc(&m, name, HI5622_IFTYPE_AP,
					   &slot) == 0);
	}
	CHECK(hi5622_cfg_vap_count(&m) == HI5622_CFG_MAX_VAPS);
	CHECK(hi5622_cfg_vap_alloc(&m, "extra0", HI5622_IFTYPE_AP, &slot) ==
	      -ENOSPC);

	/* teardown safety: removing latches */
	m.removing = true;
	CHECK(hi5622_cfg_vap_alloc(&m, "late0", HI5622_IFTYPE_AP, &slot) ==
	      -ESHUTDOWN);
	hi5622_cfg_model_deinit(&m);
	CHECK(hi5622_cfg_vap_count(&m) == 0);
}

static void test_reg_gate(void)
{
	struct hi5622_cfg_model m;

	hi5622_cfg_model_init(&m, HI5622_BAND_5GHZ);
	CHECK(hi5622_cfg_reg_gate(&m, false, 0, false, false) ==
	      HI5622_REG_DISABLED);
	m.wiphy_allocated = true;
	CHECK(hi5622_cfg_reg_gate(&m, false, 0, false, false) ==
	      HI5622_REG_WAIT);
	CHECK(hi5622_cfg_reg_gate(&m, true, 0, false, false) ==
	      HI5622_REG_WAIT);
	CHECK(hi5622_cfg_reg_gate(&m, true, 3, true, true) == HI5622_REG_WAIT);
	CHECK(hi5622_cfg_reg_gate(&m, true, 4, false, true) ==
	      HI5622_REG_WAIT_READY);
	CHECK(hi5622_cfg_reg_gate(&m, true, 4, true, false) ==
	      HI5622_REG_WAIT_READY);
	CHECK(hi5622_cfg_reg_gate(&m, true, 4, true, true) ==
	      HI5622_REG_NOW);
	CHECK(hi5622_cfg_reg_gate(&m, true, 5, true, true) ==
	      HI5622_REG_NOW);
	m.wiphy_registered = true;
	CHECK(hi5622_cfg_reg_gate(&m, true, 5, true, true) ==
	      HI5622_REG_DONE);
	m.removing = true;
	CHECK(hi5622_cfg_reg_gate(&m, true, 5, true, true) ==
	      HI5622_REG_DISABLED);
	CHECK_STREQ(hi5622_cfg_reg_action_name(HI5622_REG_WAIT), "wait-boot");
	CHECK_STREQ(hi5622_cfg_reg_action_name(HI5622_REG_NOW), "register");
	CHECK_STREQ(hi5622_cfg_reg_action_name(HI5622_REG_DONE), "registered");
}

static void test_op_map(void)
{
	static const char *const expected[] = {
		"add_virtual_intf", "del_virtual_intf", "change_virtual_intf",
		"add_key", "get_key", "del_key", "set_default_key",
		"set_default_mgmt_key", "start_ap", "change_beacon", "stop_ap",
		"add_station", "del_station", "change_station", "get_station",
		"dump_station", "change_bss", "scan", "connect", "disconnect",
		"set_wiphy_params", "set_bitrate_mask", "dump_survey",
		"set_pmksa", "del_pmksa", "flush_pmksa", "remain_on_channel",
		"cancel_remain_on_channel", "mgmt_tx", "mgmt_tx_cancel_wait",
		"set_power_mgmt", "update_mgmt_frame_registrations",
		"sched_scan_start", "sched_scan_stop", "get_channel",
		"start_p2p_device", "stop_p2p_device", "set_mac_acl",
		"external_auth",
	};
	const struct hi5622_op_map_entry *map;
	const struct hi5622_op_map_entry *e;
	size_t n = 0, i;
	int by_ms[HI5622_M_COUNT];
	int impl = 0, stub = 0, deferred = 0;

	map = hi5622_cfg_op_map(&n);
	CHECK(map != NULL);
	CHECK(n == sizeof(expected) / sizeof(expected[0]));
	CHECK(n == 39);	/* the vendor's populated op set (wifi-driver-re 2.2) */
	memset(by_ms, 0, sizeof(by_ms));
	for (i = 0; i < n; i++) {
		e = &map[i];
		CHECK(e->op != NULL && e->op[0] != 0);
		CHECK(e->milestone < HI5622_M_COUNT);
		CHECK(e->impl <= HI5622_OP_IMPL);
		CHECK(e->hcc_msg != NULL);
		by_ms[e->milestone]++;
		if (e->impl == HI5622_OP_IMPL)
			impl++;
		else if (e->impl == HI5622_OP_STUB)
			stub++;
		else
			deferred++;
		/* unknown-HCC sentinel consistency */
		if (e->hcc_group == 0xff)
			CHECK(e->hcc_id == 0xffff);
		CHECK(e->hcc_group == 0xff || e->hcc_group <= 3);
	}
	/* M0-M2 are this lane's delivered scope: 3 M1 ops + 4 M2 rows */
	CHECK(by_ms[HI5622_M0] + by_ms[HI5622_M1] + by_ms[HI5622_M2] == 7);
	CHECK(impl == 3);
	CHECK(stub == 3);
	CHECK(deferred == 33);

	e = hi5622_cfg_op_map_find("add_virtual_intf");
	CHECK(e != NULL);
	CHECK(e->milestone == HI5622_M1);
	CHECK(e->impl == HI5622_OP_IMPL);
	CHECK(e->hcc_group == 0x00 && e->hcc_id == 0x0007);
	e = hi5622_cfg_op_map_find("del_virtual_intf");
	CHECK(e != NULL && e->milestone == HI5622_M1 &&
	      e->impl == HI5622_OP_IMPL);
	CHECK(e->hcc_id == 0x0009);
	e = hi5622_cfg_op_map_find("change_virtual_intf");
	CHECK(e != NULL && e->milestone == HI5622_M1 &&
	      e->impl == HI5622_OP_IMPL);
	e = hi5622_cfg_op_map_find("scan");
	CHECK(e != NULL && e->milestone == HI5622_M2 &&
	      e->impl == HI5622_OP_STUB);
	CHECK(e->hcc_group == 0x00 && e->hcc_id == 0x000c);
	e = hi5622_cfg_op_map_find("get_station");
	CHECK(e != NULL && e->milestone == HI5622_M2 &&
	      e->impl == HI5622_OP_STUB);
	e = hi5622_cfg_op_map_find("dump_station");
	CHECK(e != NULL && e->impl == HI5622_OP_STUB);
	e = hi5622_cfg_op_map_find("start_ap");
	CHECK(e != NULL && e->milestone == HI5622_M3 &&
	      e->impl == HI5622_OP_DEFERRED);
	e = hi5622_cfg_op_map_find("connect");
	CHECK(e != NULL && e->milestone == HI5622_M5);
	e = hi5622_cfg_op_map_find("external_auth");
	CHECK(e != NULL && e->milestone == HI5622_M6);
	CHECK(hi5622_cfg_op_map_find("not_an_op") == NULL);
	CHECK(hi5622_cfg_op_map_find(NULL) == NULL);

	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
		CHECK(hi5622_cfg_op_map_find(expected[i]) != NULL);

	CHECK_STREQ(hi5622_cfg_op_impl_name(HI5622_OP_STUB),
		    "stub-eopnotsupp");
	CHECK(strstr(hi5622_cfg_milestone_name(HI5622_M2), "M2") != NULL);
}

static void test_msg_refs(void)
{
	const struct hi5622_hcc_msg_ref *refs;
	const struct hi5622_hcc_msg_ref *r;
	size_t n = 0, i;

	refs = hi5622_cfg_hcc_msg_refs(&n);
	CHECK(refs != NULL);
	CHECK(n >= 15);
	for (i = 0; i < n; i++) {
		r = &refs[i];
		CHECK(r->name != NULL && r->name[0] != 0);
		CHECK(r->builder != NULL);
		CHECK(r->group <= 3);
		CHECK(r->milestone < HI5622_M_COUNT);
	}
	/* the proven pair and the task-103 pending rows */
	r = &refs[0];
	CHECK(r->group == 0x00 && r->id == 0x0001 && r->payload_len == 0x14);
	CHECK(strstr(r->builder, "exact") != NULL);
	r = &refs[2];
	CHECK(r->group == 0x00 && r->id == 0x0007 && r->payload_len == 168);
	CHECK(strstr(r->builder, "pending") != NULL);
	r = &refs[5];
	CHECK(r->group == 0x00 && r->id == 0x000c && r->payload_len == 508);
	CHECK(strstr(r->builder, "task-103") != NULL);
	r = &refs[4];
	CHECK(r->group == 0x00 && r->id == 0x0009 && r->payload_len == 12);
	CHECK(strstr(r->builder, "exact") != NULL);
}

static void test_status_line(void)
{
	struct hi5622_cfg_model m;
	char buf[2048];
	int n;

	hi5622_cfg_model_init(&m, HI5622_BAND_2GHZ);
	m.wiphy_allocated = true;
	m.wiphy_registered = true;
	(void)hi5622_cfg_vap_alloc(&m, "vap0", HI5622_IFTYPE_AP, &(u16){0});
	(void)hi5622_cfg_vap_alloc(&m, "vap1", HI5622_IFTYPE_STATION,
				   &(u16){0});
	n = hi5622_cfg_status_line(&m, buf, sizeof(buf));
	CHECK(n > 0);
	CHECK((size_t)n == strlen(buf));
	CHECK(strstr(buf, "band=2.4GHz") != NULL);
	CHECK(strstr(buf, "registered=1") != NULL);
	CHECK(strstr(buf, "vap0: vap0") != NULL);
	CHECK(strstr(buf, "vap1: vap1") != NULL);
	CHECK(strstr(buf, "deferred=2") != NULL);

	n = hi5622_cfg_status_line(&m, buf, 64);
	CHECK(n > 0 && n < 64);
	CHECK(strlen(buf) == (size_t)n);
	n = hi5622_cfg_status_line(&m, buf, 1);
	CHECK(n == 0);
	CHECK(buf[0] == 0);
	CHECK(hi5622_cfg_status_line(NULL, buf, sizeof(buf)) == 0);
}

int main(void)
{
	printf("wifi-cfg80211 logic selftest (device-free)\n");
	test_model_basics();
	test_vap_lifecycle();
	test_reg_gate();
	test_op_map();
	test_msg_refs();
	test_status_line();
	printf("%d/%d checks passed\n", g_checks - g_failed, g_checks);
	if (g_failed) {
		printf("FAILED: %d check(s)\n", g_failed);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
