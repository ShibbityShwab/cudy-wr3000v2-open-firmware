// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_ete.c - TX/RX ETE ring service of the clean-room Hi5622V100 host
 * driver skeleton.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from
 * the transport interface contract (phase37 2.1: every H2D HCC frame rides one
 * SR0 node plus the id-3 announce; phase37 3.1: D2H ids 3/6/7 drain/wake the
 * RX path) and wifi-driver-re section 4.1.  No vendor source or object code.
 *
 * The transport owns the SR0/DR0 ring buffers, the channel register writes and
 * the node format; this file is the driver-side service wrapper that counts,
 * gates and routes.  It never touches a BAR.
 */
#include <linux/kernel.h>
#include <linux/string.h>

#include "hi5622_drv.h"

int hi5622_ete_init(struct hi5622_chip *chip)
{
	const struct hi5622_transport_ops *ops = chip->ops;
	int ret;

	if (!ops || !ops->ete_init)
		return -EOPNOTSUPP;
	if (chip->ete.inited)
		return 0;

	ret = ops->ete_init(chip->tr);
	if (ret) {
		dev_err(chip->dev, "ete: transport ring init failed: %d\n", ret);
		return ret;
	}

	chip->ete.inited = true;
	chip->ete.depth = ops->ete_depth(chip->tr);
	chip->ete.buf_len = ops->ete_buf_len(chip->tr);
	chip->hcc.max_frame = chip->ete.buf_len;

	dev_info(chip->dev,
		 "ete: rings up (SR0/DR0 depth %u, buffer %u B, HCC max %u B)\n",
		 chip->ete.depth, chip->ete.buf_len, chip->hcc.max_frame);
	return 0;
}

void hi5622_ete_exit(struct hi5622_chip *chip)
{
	/* The transport frees the rings at remove; nothing device-side to undo
	 * here (the driver never owned the BAR or the DMA allocations). */
	chip->ete.inited = false;
}

bool hi5622_ete_ready(const struct hi5622_chip *chip)
{
	return chip->ete.inited;
}

int hi5622_ete_tx_hcc(struct hi5622_chip *chip, const void *data, u32 len)
{
	const struct hi5622_transport_ops *ops = chip->ops;
	int ret;

	if (!ops || !ops->h2d_send)
		return -EOPNOTSUPP;
	if (!chip->ete.inited) {
		chip->ete.tx_errors++;
		return -ENODEV;
	}

	ret = ops->h2d_send(chip->tr, data, len);
	if (ret)
		chip->ete.tx_errors++;
	else
		chip->ete.tx_posts++;
	return ret;
}

int hi5622_ete_rx_service(struct hi5622_chip *chip)
{
	const struct hi5622_transport_ops *ops = chip->ops;
	int n;

	if (!ops || !ops->ete_rx_service)
		return -EOPNOTSUPP;
	if (!chip->ete.inited) {
		chip->ete.rx_errors++;
		return -ENODEV;
	}

	n = ops->ete_rx_service(chip->tr);
	if (n < 0) {
		chip->ete.rx_errors++;
		return n;
	}
	chip->ete.rx_payloads += n;
	return n;
}
