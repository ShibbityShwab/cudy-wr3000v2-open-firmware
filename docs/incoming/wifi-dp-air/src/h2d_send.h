/* SPDX-License-Identifier: GPL-2.0 */
/*
 * h2d_send.h - kernel surface of the first REAL post-ready H2D message path
 * (Hi5622V100 clean-room driver, wifi-h2d-send lane/task-98).
 *
 * The policy/emission logic lives in h2d_send_logic.c (portable, host-tested);
 * this header only exposes the sysfs group and the module parameter.
 */
#ifndef H2D_SEND_H
#define H2D_SEND_H

#include <linux/device.h>
#include <linux/types.h>

/* Per-load real-frame budget: 0 = negative control (no device write),
 * 1 = the bench default ("one frame per idea"), max 8 for a supervised
 * multi-message session. */
extern unsigned int hi5622_h2d_send_budget;

/* The data-plane hand-off (g0 id1 key 0x0320): 4-byte device VA of the
 * host-DRAM TXQ table, committed through the same exact-frame carrier/gates as
 * the other post-ready messages.  Called by hi5622_dp.c only. */
int  h2d_post_txq_addr(struct hi5622_pci *ctx, u32 txq_devva);

int  hi5622_h2d_add_attrs(struct device *dev);
void hi5622_h2d_del_attrs(struct device *dev);

#endif /* H2D_SEND_H */
