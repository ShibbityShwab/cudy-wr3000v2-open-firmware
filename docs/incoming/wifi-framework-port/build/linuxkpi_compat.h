/*
 * linuxkpi_compat.h - forced-include compatibility shim for the hi3881-derived
 * WLAN framework on Linux 6.18.
 *
 * 6.18 removals this shim covers:
 *   - include/linux/version.h removed in 6.0: restore the version macros the
 *     upstream code's '#if (LINUX_VERSION_CODE >= kernel_version(a,b,c))' guards
 *     rely on (oal_util.h also defines them; both are #ifndef-guarded).
 *   - 'struct timeval' removed from kernel space (y2038 work): dmac_ext_if.h
 *     and oal_util.h embed it in on-the-wire/descriptor structs, so provide a
 *     compatible layout.
 *
 * Port-shim written by the port lane; contains no upstream-tree code.
 * For kernel use the port is GPL-2.0 (same license family as the hi3881 driver
 * files it is compiled with).
 */
#ifndef __LINUXKPI_COMPAT_H__
#define __LINUXKPI_COMPAT_H__

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + ((c) > 255 ? 255 : (c)))
#endif

#ifndef kernel_version
#define kernel_version(a, b, c) KERNEL_VERSION(a, b, c)
#endif

#ifndef LINUX_VERSION_CODE
#include <generated/uapi/linux/version.h>
#endif

/* 6.18: struct timeval is no longer defined for kernel space */
struct timeval {
	long	tv_sec;
	long	tv_usec;
};

#endif /* __LINUXKPI_COMPAT_H__ */
