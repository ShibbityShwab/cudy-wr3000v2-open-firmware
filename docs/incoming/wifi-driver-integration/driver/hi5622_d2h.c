// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_d2h.c - D2H dispatch table of the clean-room Hi5622V100 host driver
 * skeleton: mailbox-layer ids 0..10 (transport D2H bitmap) and the HCC
 * group-4 boot/ready/heartbeat table.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from
 * phase37/THE-PROTOCOL-MAP.md sections 1.2/3, wifi-driver-re 3.5 and the
 * post-B1 authoritative re-decode in wifi-hcc-re/REPORT.md sections 2/5
 * (task-63, submodule commit 07b9201) - all device-grade [V].  No vendor
 * source or object code.
 *
 * Two layers, both implemented here:
 *
 *  A. Mailbox-layer ids (the transport D2H bitmap at CA 0x40039014, host
 *     dispatch ids 0..10).  The vendor host registers exactly {1, 3, 6, 7}
 *     and replies to none of them (phase37 3.1 [V]):
 *       1  pcie_dev_ready_msg_handle            - empty stub (the real ready
 *                                                  work is HCC group 4)
 *       3  pcie_ete_transfer_done_handle        - drain completed DR buffers
 *       6  pcie_trigger_ete_sending_handle      - wake the SR/TX pump
 *       7  pcie_trigger_ete_sending_handle      - same handler [V]
 *     This skeleton additionally observes ids 2 and 4 (marked [I]): bit 2 is
 *     the device's ETE-bring-up announce on the bench (phase37 1.1 step 4)
 *     and the transport skeleton uses it as a ready signal; hcc-re 5.3 [V]
 *     confirms the vendor PCIe layer has NO handler for id 2 (dropped, no
 *     reply) - it is the platform-up announce, not the ready handshake.  The
 *     two observability registrations ({2,4}) are an intentional skeleton
 *     addition for the B3 session; they are not vendor behaviour.
 *
 *  B. HCC chip-space group-4 table (the payload that rides a D2H transfer;
 *     phase37 3.2 / hcc-re 2.4 [V], registration by plat_init_bal_hcc_excp,
 *     count 5): id 1 device_plat_ready releases the platform-ready wait, id 2
 *     host_ready releases the DEVICE READY wait (payload <= 0x63 B).  These
 *     are the load-bearing boot completes and the handoff to hcc_msg_process
 *     happens ONLY via these DR-ring payloads; the mailbox ids above carry no
 *     body and no reply.  The vendor sends nothing to make ready happen.
 */
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "hi5622_drv.h"

/* The 11-entry mailbox-layer dispatch table (ids 0..10).  ids that the
 * vendor leaves unregistered are listed with their contract meaning only; the
 * skeleton registers the vendor set plus the two observability ids below. */
static const char *const hi5622_d2h_names[HI5622_TR_D2H_IDS] = {
	[0]  = "unused",
	[1]  = "pcie_dev_ready_msg_handle",
	[2]  = "device ETE-bringup announce [U]",
	[3]  = "pcie_ete_transfer_done_handle",
	[4]  = "heartbeat window [U]",
	[5]  = "unused (id 5 is H2D-forbidden)",
	[6]  = "pcie_trigger_ete_sending_handle",
	[7]  = "pcie_trigger_ete_sending_handle",
	[8]  = "unused",
	[9]  = "unused",
	[10] = "unused",
};

static const char *hi5622_d2h_name(u32 id)
{
	return id < HI5622_TR_D2H_IDS ? hi5622_d2h_names[id] : "out-of-range";
}

/* ids this skeleton installs: the vendor set + the two observability ids */
static const u32 hi5622_d2h_registered[] = { 1, 2, 3, 4, 6, 7 };

static void hi5622_d2h_bump(struct hi5622_chip *chip, u32 id)
{
	struct hi5622_d2h *d2h = &chip->d2h;
	unsigned long flags;

	if (id >= HI5622_TR_D2H_IDS)
		return;
	spin_lock_irqsave(&d2h->lock, flags);
	d2h->count[id]++;
	d2h->last_id = id;
	spin_unlock_irqrestore(&d2h->lock, flags);
}

/* ---- mailbox-layer handlers (atomic context) ---- */

/* id 1: vendor handler is an empty stub; the real device-ready work happens
 * on the HCC group-4 payload.  Keep it observable. */
static void hi5622_d2h_mb_dev_ready(struct hi5622_transport *t, u32 id,
				    void *arg)
{
	struct hi5622_chip *chip = arg;

	hi5622_d2h_bump(chip, id);
	dev_info_ratelimited(chip->dev,
			     "d2h: mailbox id %u (%s; HCC group 4 carries the real ready)\n",
			     id, hi5622_d2h_name(id));
}

/* id 2: the device's ETE bring-up announce.  The vendor mailbox layer has no
 * handler here; the transport skeleton treats it as a ready signal [U]. */
static void hi5622_d2h_mb_announce(struct hi5622_transport *t, u32 id,
				   void *arg)
{
	struct hi5622_chip *chip = arg;

	hi5622_d2h_bump(chip, id);
	dev_info_ratelimited(chip->dev,
			     "d2h: mailbox id 2 (%s; see phase37 1.1 step 4)\n",
			     hi5622_d2h_name(id));
}

/* id 3: completed D2H transfers -> drain the DR ring (payload -> hcc) */
static void hi5622_d2h_mb_transfer_done(struct hi5622_transport *t, u32 id,
					void *arg)
{
	struct hi5622_chip *chip = arg;
	int n;

	hi5622_d2h_bump(chip, id);
	n = hi5622_ete_rx_service(chip);
	dev_dbg_ratelimited(chip->dev,
			    "d2h: id 3 drained %d DR payload(s)\n", n);
}

/* id 6/7: wake the SR/TX pump and service the RX path */
static void hi5622_d2h_mb_trigger_tx(struct hi5622_transport *t, u32 id,
				     void *arg)
{
	struct hi5622_chip *chip = arg;

	hi5622_d2h_bump(chip, id);
	hi5622_hcc_kick(chip);
	hi5622_ete_rx_service(chip);
}

/* id 4: heartbeat window [U]; count and keep quiet */
static void hi5622_d2h_mb_heartbeat(struct hi5622_transport *t, u32 id,
				    void *arg)
{
	struct hi5622_chip *chip = arg;

	hi5622_d2h_bump(chip, id);
	dev_dbg_ratelimited(chip->dev, "d2h: mailbox id 4 observed\n");
}

static const struct {
	u32	id;
	void	(*fn)(struct hi5622_transport *t, u32 id, void *arg);
} hi5622_d2h_handlers[] = {
	{ 1, hi5622_d2h_mb_dev_ready },
	{ 2, hi5622_d2h_mb_announce },
	{ 3, hi5622_d2h_mb_transfer_done },
	{ 4, hi5622_d2h_mb_heartbeat },
	{ 6, hi5622_d2h_mb_trigger_tx },
	{ 7, hi5622_d2h_mb_trigger_tx },
};

/* ---- HCC group 4: boot / ready / heartbeat / exception (phase37 3.2 [V]) ---- */

static void hi5622_hcc_boot_dev_log(struct hi5622_chip *chip,
				    const struct hi5622_hcc_rx *rx, void *aux)
{
	/* vendor: oam_rx_post_action_function forwards a device log blob to
	 * SDT/netlink; the skeleton bounds-checks and logs the size only. */
	dev_info_ratelimited(chip->dev,
			     "hcc: device log blob group %u id %u (%u B)\n",
			     rx->group, rx->id, rx->len);
}

static void hi5622_hcc_boot_plat_ready(struct hi5622_chip *chip,
				       const struct hi5622_hcc_rx *rx,
				       void *aux)
{
	/* group 4 id 1, device_plat_ready_msg_process: releases wait #1 [V] */
	dev_info(chip->dev, "Device plat ready! (HCC group %u id %u, %u B)\n",
		 rx->group, rx->id, rx->len);
	hi5622_chip_set_state(chip, HI5622_DRV_STATE_PLAT_READY,
			      "hcc plat-ready");
	complete(&chip->plat_ready);
}

static void hi5622_hcc_boot_host_ready(struct hi5622_chip *chip,
				       const struct hi5622_hcc_rx *rx,
				       void *aux)
{
	/* group 4 id 2, host_ready_msg_process: releases wait #2 [V] */
	dev_info(chip->dev, "DEVICE READY (HCC group %u id %u, %u B)\n",
		 rx->group, rx->id, rx->len);
	hi5622_chip_set_state(chip, HI5622_DRV_STATE_DEVICE_READY,
			      "hcc device-ready");
	complete(&chip->device_ready);
	queue_work(chip->wq, &chip->setup_work);
}

static void hi5622_hcc_boot_exception(struct hi5622_chip *chip,
				      const struct hi5622_hcc_rx *rx, void *aux)
{
	dev_warn_ratelimited(chip->dev,
			     "hcc: device exception info group %u id %u (%u B)\n",
			     rx->group, rx->id, rx->len);
}

static void hi5622_hcc_boot_heartbeat(struct hi5622_chip *chip,
				      const struct hi5622_hcc_rx *rx, void *aux)
{
	dev_dbg_ratelimited(chip->dev, "hcc: device heartbeat\n");
}

static const struct hi5622_hcc_reg hi5622_hcc_boot_tab[] = {
	{ HI5622_HCC_BOOT_DEV_LOG,      0, hi5622_hcc_boot_dev_log,      NULL },
	{ HI5622_HCC_BOOT_PLAT_READY,   0, hi5622_hcc_boot_plat_ready,   NULL },
	{ HI5622_HCC_BOOT_HOST_READY,   0, hi5622_hcc_boot_host_ready,   NULL },
	{ HI5622_HCC_BOOT_EXCEPTION,    0, hi5622_hcc_boot_exception,    NULL },
	{ HI5622_HCC_BOOT_HEARTBEAT,    0, hi5622_hcc_boot_heartbeat,    NULL },
};

int hi5622_d2h_register_boot_table(struct hi5622_chip *chip)
{
	/* plat.ko registers group 4 with 5 entries (phase37 3.2 [V]). */
	return hi5622_hcc_register_tab_chip(&chip->hcc,
					    HI5622_HCC_GROUP_CHIP_READY,
					    hi5622_hcc_boot_tab,
					    ARRAY_SIZE(hi5622_hcc_boot_tab));
}

/* ---- lifecycle ---- */
int hi5622_d2h_init(struct hi5622_chip *chip)
{
	const struct hi5622_transport_ops *ops = chip->ops;
	unsigned int i;
	int ret;

	spin_lock_init(&chip->d2h.lock);
	chip->d2h.chip = chip;
	chip->d2h.last_id = 0;

	ret = hi5622_d2h_register_boot_table(chip);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(hi5622_d2h_handlers); i++) {
		u32 id = hi5622_d2h_handlers[i].id;

		ret = ops->d2h_register(chip->tr, id,
					hi5622_d2h_handlers[i].fn, chip);
		if (ret) {
			dev_warn(chip->dev,
				 "d2h: mailbox id %u registration failed: %d\n",
				 id, ret);
			continue;
		}
	}

	dev_info(chip->dev,
		 "d2h: table installed (11 mailbox ids, %zu registered; HCC group 4 boot table 5 ids)\n",
		 ARRAY_SIZE(hi5622_d2h_handlers));
	return 0;
}

void hi5622_d2h_exit(struct hi5622_chip *chip)
{
	const struct hi5622_transport_ops *ops = chip->ops;
	unsigned int i;

	if (!ops || !ops->d2h_unregister)
		return;
	for (i = 0; i < ARRAY_SIZE(hi5622_d2h_registered); i++)
		ops->d2h_unregister(chip->tr, hi5622_d2h_registered[i]);
}

void hi5622_d2h_rx_frame(struct hi5622_chip *chip, const void *buf, u32 len)
{
	chip->d2h.frames++;
	chip->rx_frames++;
	if (!buf || len < HI5622_HCC_HDR_LEN) {
		chip->d2h.dropped++;
		return;
	}

	/* The hdpp layer owns the HCC demux entry point (hi5622_hcc_process)
	 * and its per-class bookkeeping. */
	hi5622_hdpp_rx_frame(chip, buf, len);
}

u32 hi5622_d2h_count(const struct hi5622_chip *chip, u32 id)
{
	if (id >= HI5622_TR_D2H_IDS)
		return 0;
	return READ_ONCE(chip->d2h.count[id]);
}
