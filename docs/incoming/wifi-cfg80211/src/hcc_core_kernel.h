/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hcc_core_kernel.h - reference kernel adapter for the portable hcc_core
 * state machine (wifi-hcc-core lane, task-92).
 *
 * The adapter is deliberately thin: it maps the core's byte-level IO ops to
 * readl()/writel() on the endpoint BAR0 mapping, converts jiffies to the
 * core's ms clock, and routes core events to dev_info().  Integration into
 * hi5622_pciet_hcc only needs the three calls in section 3 of INTEGRATION.md.
 */
#ifndef HCC_CORE_KERNEL_H
#define HCC_CORE_KERNEL_H

#include <linux/device.h>
#include <linux/io.h>
#include <linux/types.h>

#include "hcc_core.h"

struct hcc_kernel {
	struct hcc_core		core;
	struct hcc_io_ops	ops;
	void __iomem		*bar0;
	struct device		*dev;
	unsigned int		log_limit;	/* max dev_info events, 0 = all */
	unsigned int		logged;
};

/* Initialise the adapter (and the embedded core) on an ioremapped BAR0. */
void hcc_kernel_init(struct hcc_kernel *k, struct device *dev,
		     void __iomem *bar0, const struct hcc_core_config *cfg);

/* jiffies -> the core's millisecond clock (HZ-independent). */
hcc_u64 hcc_kernel_now_ms(void);

/* One service pass at the current time. */
int hcc_kernel_tick(struct hcc_kernel *k);

/* Record the transport's 0x5a5a release. */
void hcc_kernel_release(struct hcc_kernel *k);

/* Status line for a sysfs/dmesg consumer. */
int hcc_kernel_status(struct hcc_kernel *k, char *buf, hcc_u32 len);

#endif /* HCC_CORE_KERNEL_H */
