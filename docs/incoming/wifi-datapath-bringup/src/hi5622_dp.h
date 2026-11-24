/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dp.h - MAC host-interface data-plane bring-up surface for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (wifi-datapath-bringup lane, shared task task-111).
 * This header only declares the probe/remove and sysfs-group hooks; the state
 * and all device access live in hi5622_dp.c, the portable geometry in
 * dp_rings.c.
 *
 * Bench model (unchanged project rules): probe() performs NO device writes;
 * every device write is an explicit attended sysfs action behind the
 * boot-stage gates, one idea per session, no retry after a failure.
 */
#ifndef _HI5622_DP_H
#define _HI5622_DP_H

struct hi5622_pci;
struct device;

/* Allocate the (empty) data-plane state at PCI probe.  Performs no device
 * access and allocates no rings: the 0x63c/0xb0 buffer pools are allocated by
 * dp_setup only after the ready handshake and the context-create succeeded. */
int  hi5622_dp_probe(struct hi5622_pci *ctx);
void hi5622_dp_remove(struct hi5622_pci *ctx);

int  hi5622_dp_add_attrs(struct device *dev);
void hi5622_dp_del_attrs(struct device *dev);

#endif /* _HI5622_DP_H */
