// SPDX-License-Identifier: GPL-2.0
/*
 * hcc_core_kernel.c - reference kernel adapter for hcc_core (task-92).
 *
 * Everything device-touching lives in the core; this file only supplies the
 * BAR0 readl/writel ops, the jiffies clock and event logging.  It performs no
 * device access by itself and has no module_init: the compile-check module
 * sources/kmod/hcc_core_check.c links it and the core into a kernel module,
 * which is how this file is compile-verified against the 6.18.60 tree.
 */
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "hcc_core_kernel.h"

static hcc_u32 hcc_kernel_read32(void *priv, hcc_u32 off)
{
	struct hcc_kernel *k = priv;

	return (hcc_u32)readl(k->bar0 + off);
}

static void hcc_kernel_write32(void *priv, hcc_u32 off, hcc_u32 val)
{
	struct hcc_kernel *k = priv;

	writel((u32)val, k->bar0 + off);
}

static void hcc_kernel_event(void *priv, enum hcc_event ev, hcc_u32 data)
{
	struct hcc_kernel *k = priv;

	if (k->log_limit && k->logged >= k->log_limit)
		return;
	k->logged++;
	dev_info(k->dev, "hcc-core: %s data=0x%08x (tick %llu)\n",
		 hcc_event_name(ev), data,
		 (unsigned long long)k->core.now_ms);
}

void hcc_kernel_init(struct hcc_kernel *k, struct device *dev,
		     void __iomem *bar0, const struct hcc_core_config *cfg)
{
	memset(k, 0, sizeof(*k));
	k->dev = dev;
	k->bar0 = bar0;
	k->ops.priv = k;
	k->ops.read32 = hcc_kernel_read32;
	k->ops.write32 = hcc_kernel_write32;
	k->log_limit = 128;
	hcc_core_init(&k->core, &k->ops, cfg, hcc_kernel_event, k);
}

hcc_u64 hcc_kernel_now_ms(void)
{
	return (hcc_u64)jiffies_to_msecs(jiffies);
}

int hcc_kernel_tick(struct hcc_kernel *k)
{
	return hcc_core_tick(&k->core, hcc_kernel_now_ms());
}

void hcc_kernel_release(struct hcc_kernel *k)
{
	hcc_core_note_release(&k->core, hcc_kernel_now_ms());
}

int hcc_kernel_status(struct hcc_kernel *k, char *buf, hcc_u32 len)
{
	return hcc_core_status(&k->core, buf, len);
}
