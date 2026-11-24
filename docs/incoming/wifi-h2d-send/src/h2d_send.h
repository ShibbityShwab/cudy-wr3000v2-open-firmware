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

int  hi5622_h2d_add_attrs(struct device *dev);
void hi5622_h2d_del_attrs(struct device *dev);

#endif /* H2D_SEND_H */
