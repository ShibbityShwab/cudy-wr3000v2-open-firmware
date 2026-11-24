// SPDX-License-Identifier: GPL-2.0
/*
 * hcc_core_check.c - kernel compile-check module for the hcc_core library
 * (wifi-hcc-core lane, task-92).
 *
 * HOW IT IS USED: tools/build.sh --kernel builds this as hcc_core_check.ko
 * against the project's 6.18.60 tree.  The goal is compile verification and a
 * device-free frame-contract smoke at insmod time; the module never touches a
 * device (its IO ops are NULL) and is NOT part of the hcc service module.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "hcc_core.h"

static struct hcc_core hcc_check_core;

static int __init hcc_core_check_init(void)
{
	struct hcc_core *c = &hcc_check_core;
	struct hcc_io_ops ops;
	struct hcc_core_config cfg;
	struct hcc_frame f;
	hcc_u8 buf[32];
	hcc_u32 total = 0;
	int rc;

	memset(&ops, 0, sizeof(ops));
	hcc_core_config_default(&cfg);
	hcc_core_init(c, &ops, &cfg, NULL, NULL);

	rc = hcc_core_frame_build(c, buf, sizeof(buf), 0u, 0u, 0u,
				  NULL, 0u, &total);
	if (rc || total != HCC_FRAME_HDR)
		return -EINVAL;
	rc = hcc_core_frame_decode(buf, total, &f);
	if (rc || f.group != 0u || f.id != 0u || f.tag != HCC_FRAME_TAG)
		return -EINVAL;
	if (hcc_bar0_io(HCC_CA_D2H_ACK) != 0x4b9438u)
		return -EINVAL;
	pr_info("hcc_core compile check: frame contract + address map OK\n");
	return 0;
}

static void __exit hcc_core_check_exit(void)
{
}

module_init(hcc_core_check_init);
module_exit(hcc_core_check_exit);

MODULE_LICENSE("GPL");
/* the luofu tree sets CONFIG_MODULE_STRIPPED, which strips
 * MODULE_DESCRIPTION(); use MODULE_INFO directly so modpost stays clean */
MODULE_INFO(description,
	    "hcc_core compile check + device-free frame contract smoke");
MODULE_AUTHOR("wifi-hcc-core lane (clean-room)");
