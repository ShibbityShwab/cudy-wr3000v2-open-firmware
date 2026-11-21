/*
 * skel_mod.c - module glue for the hi3881-derived framework SKELETON build.
 * Written by the port lane (wifi-framework-port); no upstream-tree code.
 * License: GPL-2.0 (kernel module glue for the GPL-2.0 hi3881 driver files).
 */
#include <linux/module.h>
#include "frw_main.h"
#include "oam_ext_if.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("hi3881-derived WLAN framework skeleton, Linux 6.18 port experiment (census lane)");
MODULE_AUTHOR("cudy-wr3000v2 wifi-framework-port lane");

/* oal/exception_rst.c is HDF-bound (oal_chr.h -> hi_wifi_api.h -> hdf_ibus_intf.h)
 * and left out of the skeleton; stub its only frw-visible symbol here.
 */
void oal_frw_exception_report(void)
{
	/* upstream: hisi_sched_event(DRIVER_HUNG) via OpenHarmony HDF */
}

static int __init hi3881_fw_skel_init(void)
{
	pr_info("hi3881_fw_skel: skeleton loaded (libsec+frw+oal_mem+oam_log+mac/common)\n");
	return 0;
}

static void __exit hi3881_fw_skel_exit(void)
{
	pr_info("hi3881_fw_skel: skeleton unloaded\n");
}

module_init(hi3881_fw_skel_init);
module_exit(hi3881_fw_skel_exit);
