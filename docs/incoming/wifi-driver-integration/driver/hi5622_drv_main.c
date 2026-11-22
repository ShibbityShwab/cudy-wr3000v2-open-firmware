// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_drv_main.c - module glue of the clean-room Hi5622V100 hdpp-style
 * Wi-Fi driver skeleton: transport client binding, chip lifecycle, state
 * machine and the attended-bench write gate.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) on top
 * of interface/hi5622_transport_iface.h.  No vendor source or object code.
 *
 * Bind sequence:
 *   module_init -> hi5622_transport_register_client()
 *     transport probes later  -> client.attach(tr, ops)
 *       hcc init -> d2h table init -> hdpp/alg init -> wiphy stub -> netdev
 *       -> wait for the HCC group-4 boot completes (PLAT_READY/DEVICE_READY)
 *       -> setup_work -> hdpp channel setup (only when allow_device_writes=1)
 *       -> RUNNING
 *     transport removed/unloaded -> client.detach(tr)
 *
 * The transport module keeps ownership of the PCI device and every BAR; this
 * module only calls the ops table.  All device writes remain behind
 * allow_device_writes (attended-bench gate, default off).
 */
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "hi5622_drv.h"

static bool allow_device_writes;
module_param(allow_device_writes, bool, 0444);
MODULE_PARM_DESC(allow_device_writes,
		 "open the attended-bench gate for ETE channel-register writes (default off)");

static bool create_netdev = true;
module_param(create_netdev, bool, 0444);
MODULE_PARM_DESC(create_netdev, "register the netdev skeleton per chip (default on)");

static bool alg_protocol;
module_param(alg_protocol, bool, 0444);
MODULE_PARM_DESC(alg_protocol,
		 "enable the alg cfg sync protocol whose D2H response id is still [U] (default off)");


static struct hi5622_driver_ctx hi5622_ctx;

/*
 * B3 bring-up instrument: post exactly ONE candidate HCC frame through the
 * normal H2D path (SR0 node + id-3 announce) for the chip selected by
 * bench_chip, then watch the D2H bitmap/DR ring for the device reaction.
 *
 * B2 proved the bare H2D smoke (64 B zero frame + announce id 3) is safe but
 * produced no D2H reply; a well-formed HCC header is the next variable.  The
 * candidate group/id/rgroup are module parameters because the first host
 * message is still [U] (hcc-re task-63 / phase37 2.3); defaults are the
 * group-0 id-1 config helper quoted by phase37.
 *
 * Usage (attended bench, after B1/B2 and the driver has reached RUNNING):
 *   echo 1 > /sys/module/hi5622_hdpp_drv/parameters/bench_hcc_probe
 * one frame is posted per write; set bench_hcc_group/id/rgroup between writes
 * to run the candidate matrix one variable at a time.
 */
static uint bench_chip;
module_param(bench_chip, uint, 0444);
MODULE_PARM_DESC(bench_chip, "transport/chip index for bench_hcc_probe (default 0, first endpoint)");
static uint bench_hcc_group = HI5622_HCC_GROUP_CFG;
module_param(bench_hcc_group, uint, 0644);
MODULE_PARM_DESC(bench_hcc_group, "candidate first frame HCC group (default 0)");
static uint bench_hcc_id = HI5622_HCC_ID_CFG_SEND_EVENT;
module_param(bench_hcc_id, uint, 0644);
MODULE_PARM_DESC(bench_hcc_id, "candidate first frame HCC id (default 1 = hmac/hdpp config send event)");
static uint bench_hcc_rgroup;
module_param(bench_hcc_rgroup, uint, 0644);
MODULE_PARM_DESC(bench_hcc_rgroup, "candidate first frame resource-group nibble (default 0)");

static struct hi5622_chip *hi5622_chip_by_index(uint index)
{
	struct hi5622_chip *chip;

	list_for_each_entry(chip, &hi5622_ctx.chips, node)
		if (chip->index == index)
			return chip;
	return NULL;
}

static void hi5622_bench_post_frame(struct hi5622_chip *chip)
{
	struct hi5622_hcc_msg *msg;
	u8 payload[4] = { 0, 0, 0, 0 };

	msg = hi5622_hcc_msg_alloc(sizeof(payload), GFP_KERNEL);
	if (!msg) {
		dev_err(chip->dev, "bench: no memory for the candidate frame\n");
		return;
	}
	memcpy(msg->data + HI5622_HCC_HDR_LEN, payload, sizeof(payload));
	hi5622_hcc_msg_set_hdr(msg, (u8)bench_hcc_group, (u8)bench_hcc_rgroup,
			       0, 0, (u16)bench_hcc_id, 0);
	dev_info(chip->dev,
		 "bench: posting candidate HCC frame group %u id %u rgroup %u (12+%zu B), SR0 + announce id 3 - [U]\n",
		 bench_hcc_group, bench_hcc_id, bench_hcc_rgroup,
		 sizeof(payload));
	hi5622_hcc_tx(chip, msg);
}

static int hi5622_bench_probe_set(const char *val,
				  const struct kernel_param *kp)
{
	struct hi5622_chip *chip;
	bool enable;
	int ret;

	ret = kstrtobool(val, &enable);
	if (ret)
		return ret;
	if (!enable)
		return 0;

	mutex_lock(&hi5622_ctx.lock);
	chip = hi5622_chip_by_index(bench_chip);
	if (!chip) {
		pr_warn(HI5622_DRV_NAME ": bench: no chip %u attached\n",
			bench_chip);
		ret = -ENODEV;
	} else if (!(hi5622_chip_get_state(chip) & HI5622_DRV_STATE_RUNNING)) {
		dev_warn(chip->dev,
			 "bench: chip not RUNNING yet - cannot post candidate frame\n");
		ret = -EAGAIN;
	} else {
		hi5622_bench_post_frame(chip);
	}
	mutex_unlock(&hi5622_ctx.lock);
	return ret;
}

static int hi5622_bench_probe_get(char *buf, const struct kernel_param *kp)
{
	return scnprintf(buf, PAGE_SIZE, "0\n");
}

static const struct kernel_param_ops hi5622_bench_probe_ops = {
	.set = hi5622_bench_probe_set,
	.get = hi5622_bench_probe_get,
};

/* Write-only trigger: each "1" posts one candidate frame (see above). */
module_param_cb(bench_hcc_probe, &hi5622_bench_probe_ops, NULL, 0644);
MODULE_PARM_DESC(bench_hcc_probe,
		 "write 1 to post one candidate first HCC frame ([U] B3 instrument; attended bench only)");

/* ---------------------------------------------------------------------
 * State helpers
 * ------------------------------------------------------------------- */
static const char *const hi5622_state_names[] = {
	"attached", "tables", "plat-ready", "device-ready",
	"channels", "running", "detaching", "failed",
};

const char *hi5622_chip_state_str(u32 state, char *buf, size_t len)
{
	size_t pos = 0;
	unsigned int i;

	buf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(hi5622_state_names); i++) {
		if (!(state & BIT(i)))
			continue;
		pos += scnprintf(buf + pos, len - pos, "%s%s",
				 pos ? "|" : "", hi5622_state_names[i]);
		if (pos >= len)
			break;
	}
	if (!pos)
		scnprintf(buf, len, "none");
	return buf;
}

u32 hi5622_chip_get_state(const struct hi5622_chip *chip)
{
	return READ_ONCE(chip->state);
}

/* Runs from workqueue context: invoke the state observers (they may sleep:
 * the netdev carrier update takes rtnl_lock). */
static void hi5622_state_worker(struct work_struct *work)
{
	struct hi5622_chip *chip = container_of(work, struct hi5622_chip,
						state_work);
	struct hi5622_state_notifier *n;
	unsigned long flags;
	u32 old, new;

	spin_lock_irqsave(&chip->state_lock, flags);
	old = chip->state_old_snapshot;
	new = chip->state;
	chip->state_work_queued = false;
	spin_unlock_irqrestore(&chip->state_lock, flags);

	mutex_lock(&chip->notifier_lock);
	list_for_each_entry(n, &chip->notifiers, node)
		if (n->cb)
			n->cb(chip, old, new, n->priv);
	mutex_unlock(&chip->notifier_lock);
}

void hi5622_chip_set_state(struct hi5622_chip *chip, u32 new_state,
			   const char *reason)
{
	unsigned long flags;
	bool changed = false, queue = false;

	if (!chip)
		return;
	spin_lock_irqsave(&chip->state_lock, flags);
	if ((chip->state & new_state) != new_state) {
		if (!chip->state_work_queued) {
			chip->state_old_snapshot = chip->state;
			chip->state_work_queued = true;
			queue = true;
		}
		chip->state |= new_state;
		changed = true;
	}
	spin_unlock_irqrestore(&chip->state_lock, flags);

	if (changed && chip->wq) {
		char newb[64];
		u32 cur = hi5622_chip_get_state(chip);

		dev_dbg(chip->dev, "state -> %s (%s)\n",
			hi5622_chip_state_str(cur, newb, sizeof(newb)),
			reason);
		if (queue)
			queue_work(chip->wq, &chip->state_work);
	}
}

int hi5622_chip_state_register(struct hi5622_chip *chip,
			       void (*cb)(struct hi5622_chip *, u32, u32,
					  void *), void *priv)
{
	struct hi5622_state_notifier *n;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	n->cb = cb;
	n->priv = priv;
	mutex_lock(&chip->notifier_lock);
	list_add_tail(&n->node, &chip->notifiers);
	mutex_unlock(&chip->notifier_lock);
	return 0;
}

void hi5622_chip_state_unregister(struct hi5622_chip *chip,
				  void (*cb)(struct hi5622_chip *, u32, u32,
					     void *), void *priv)
{
	struct hi5622_state_notifier *n, *tmp;

	mutex_lock(&chip->notifier_lock);
	list_for_each_entry_safe(n, tmp, &chip->notifiers, node) {
		if (n->cb == cb && n->priv == priv) {
			list_del(&n->node);
			kfree(n);
		}
	}
	mutex_unlock(&chip->notifier_lock);
}

static void hi5622_state_netdev_cb(struct hi5622_chip *chip, u32 old_state,
				   u32 new_state, void *priv)
{
	hi5622_netdev_sync_state(chip);
}

/* ---------------------------------------------------------------------
 * Chip lookup (RCU: D2H/rx callbacks are atomic, attach/detach are process
 * context; removal waits synchronize_rcu() before the chip is freed).
 * ------------------------------------------------------------------- */
static struct hi5622_chip *hi5622_chip_find_rcu(struct hi5622_transport *tr)
{
	struct hi5622_chip *chip;

	list_for_each_entry_rcu(chip, &hi5622_ctx.chips, node)
		if (chip->tr == tr)
			return chip;
	return NULL;
}

/* ---------------------------------------------------------------------
 * setup work: channels + RUNNING once DEVICE READY is in
 * ------------------------------------------------------------------- */
static void hi5622_setup_worker(struct work_struct *work)
{
	struct hi5622_chip *chip = container_of(work, struct hi5622_chip,
						setup_work);
	char buf[96];
	u32 state = hi5622_chip_get_state(chip);

	dev_info(chip->dev, "setup: entering with state %s\n",
		 hi5622_chip_state_str(state, buf, sizeof(buf)));
	if (!(state & HI5622_DRV_STATE_DEVICE_READY)) {
		dev_info(chip->dev, "setup: not device-ready yet; deferred\n");
		return;
	}
	if (hi5622_hdpp_channel_setup(chip)) {
		dev_info(chip->dev,
			 "setup: channel setup refused (attended gate / transport); staying attached\n");
		return;
	}
	hi5622_chip_set_state(chip, HI5622_DRV_STATE_CHANNELS, "channel setup");
	hi5622_chip_set_state(chip, HI5622_DRV_STATE_RUNNING, "data path nominal");
	dev_info(chip->dev,
		 "setup: chip RUNNING (hdpp state %s; tx %u rx %u frames so far)\n",
		 hi5622_hdpp_state_str(chip->hdpp.state), chip->tx_frames,
		 chip->rx_frames);
	dev_info(chip->dev,
		 "setup: B3 instrument ready - echo 1 > /sys/module/%s/parameters/bench_hcc_probe to post one candidate HCC frame\n",
		 HI5622_DRV_NAME);
}

/* ---------------------------------------------------------------------
 * Transport client callbacks
 * ------------------------------------------------------------------- */
static void hi5622_client_attach(struct hi5622_transport *tr,
				 const struct hi5622_transport_ops *ops,
				 void *priv)
{
	struct hi5622_chip *chip;
	u32 tr_state;
	int ret;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return;

	chip->dev = ops->dev;
	chip->tr = tr;
	chip->ops = ops;
	chip->index = ops->id;
	snprintf(chip->name, sizeof(chip->name), "%s-%u", HI5622_DRV_NAME,
		 chip->index);
	chip->allow_device_writes = allow_device_writes;
	spin_lock_init(&chip->state_lock);
	mutex_init(&chip->notifier_lock);
	INIT_LIST_HEAD(&chip->notifiers);
	init_completion(&chip->plat_ready);
	init_completion(&chip->device_ready);
	INIT_WORK(&chip->setup_work, hi5622_setup_worker);
	INIT_WORK(&chip->state_work, hi5622_state_worker);

	chip->wq = alloc_ordered_workqueue("%s-%u", WQ_MEM_RECLAIM,
					   HI5622_DRV_NAME, chip->index);
	if (!chip->wq) {
		dev_err(chip->dev, "attach: no workqueue\n");
		kfree(chip);
		return;
	}

	ret = hi5622_hcc_init(&chip->hcc, chip);
	if (ret)
		goto err_early;
	ret = hi5622_d2h_init(chip);
	if (ret)
		goto err_d2h;
	ret = hi5622_hdpp_init(chip);
	if (ret)
		goto err_hdpp;
	chip->hdpp.alg.enabled = alg_protocol;
	hi5622_wiphy_stub_init(chip);

	if (create_netdev) {
		ret = hi5622_netdev_register(chip);
		if (ret) {
			dev_err(chip->dev, "attach: netdev registration failed: %d\n",
				ret);
			goto err_netdev;
		}
	}

	mutex_lock(&hi5622_ctx.lock);
	list_add_tail_rcu(&chip->node, &hi5622_ctx.chips);
	mutex_unlock(&hi5622_ctx.lock);

	ret = hi5622_chip_state_register(chip, hi5622_state_netdev_cb, chip);
	if (ret)
		dev_warn(chip->dev, "attach: state notifier: %d\n", ret);

	hi5622_chip_set_state(chip,
			      HI5622_DRV_STATE_ATTACHED | HI5622_DRV_STATE_TABLES,
			      "transport attach");

	/* Late attach (transport was already running): derive the ready state
	 * from the transport and schedule setup without waiting for a D2H. */
	tr_state = ops->get_state(tr);
	if (tr_state & HI5622_TR_STATE_PLAT_READY)
		hi5622_chip_set_state(chip, HI5622_DRV_STATE_PLAT_READY,
				      "transport plat-ready");
	if (tr_state & HI5622_TR_STATE_DEVICE_READY) {
		hi5622_chip_set_state(chip, HI5622_DRV_STATE_DEVICE_READY,
				      "transport device-ready");
		queue_work(chip->wq, &chip->setup_work);
	}

	dev_info(chip->dev,
		 "attach: transport %u bound (D2H %u, SR depth %u buf %u, writes %s, netdev %s)\n",
		 chip->index, ops->d2h_count(tr), ops->ete_depth(tr),
		 ops->ete_buf_len(tr),
		 chip->allow_device_writes ? "gated-open" : "gated-closed",
		 chip->netdev ? chip->netdev->name : "none");
	if (!chip->allow_device_writes)
		dev_info(chip->dev,
			 "attach: attended gate closed - no device writes; D2H dispatch also needs hi5622_pciet.driver_mode=1\n");
	return;

err_netdev:
	hi5622_hdpp_exit(chip);
err_hdpp:
	hi5622_d2h_exit(chip);
err_d2h:
	hi5622_hcc_exit(&chip->hcc);
err_early:
	destroy_workqueue(chip->wq);
	kfree(chip);
}

static void hi5622_client_detach(struct hi5622_transport *tr, void *priv)
{
	struct hi5622_chip *chip = NULL, *cur;

	mutex_lock(&hi5622_ctx.lock);
	list_for_each_entry(cur, &hi5622_ctx.chips, node) {
		if (cur->tr == tr) {
			chip = cur;
			list_del_rcu(&chip->node);
			break;
		}
	}
	mutex_unlock(&hi5622_ctx.lock);
	if (!chip)
		return;

	hi5622_chip_set_state(chip, HI5622_DRV_STATE_DETACHING, "detach");
	cancel_work_sync(&chip->state_work);
	cancel_work_sync(&chip->setup_work);
	hi5622_chip_state_unregister(chip, hi5622_state_netdev_cb, chip);
	hi5622_netdev_unregister(chip);
	hi5622_d2h_exit(chip);
	hi5622_hcc_exit(&chip->hcc);
	hi5622_hdpp_exit(chip);
	hi5622_ete_exit(chip);
	synchronize_rcu();
	destroy_workqueue(chip->wq);
	dev_info(chip->dev, "detach: chip %u released\n", chip->index);
	kfree(chip);
}

/* Atomic context: the transport reports a state change. */
static void hi5622_client_fw_state(struct hi5622_transport *tr, u32 state,
				   u32 d2h_id, void *priv)
{
	struct hi5622_chip *chip;
	char buf[96];

	rcu_read_lock();
	chip = hi5622_chip_find_rcu(tr);
	if (chip) {
		dev_dbg_ratelimited(chip->dev, "transport state %s (d2h %u)\n",
				    hi5622_tr_state_str(state, buf, sizeof(buf)),
				    d2h_id);
		if (state & HI5622_TR_STATE_PLAT_READY)
			hi5622_chip_set_state(chip, HI5622_DRV_STATE_PLAT_READY,
					      "transport fw_state");
		if (state & HI5622_TR_STATE_DEVICE_READY) {
			hi5622_chip_set_state(chip,
					      HI5622_DRV_STATE_DEVICE_READY,
					      "transport fw_state");
			queue_work(chip->wq, &chip->setup_work);
		}
	}
	rcu_read_unlock();
}

/* Atomic context: one completed D2H HCC frame from the transport DR ring. */
static void hi5622_client_rx(struct hi5622_transport *tr, const void *buf,
			     u32 len, void *priv)
{
	struct hi5622_chip *chip;

	rcu_read_lock();
	chip = hi5622_chip_find_rcu(tr);
	if (chip)
		hi5622_d2h_rx_frame(chip, buf, len);
	rcu_read_unlock();
}

/* ---------------------------------------------------------------------
 * module init / exit
 * ------------------------------------------------------------------- */
static int __init hi5622_drv_init(void)
{
	INIT_LIST_HEAD(&hi5622_ctx.chips);
	mutex_init(&hi5622_ctx.lock);
	memset(&hi5622_ctx.client, 0, sizeof(hi5622_ctx.client));
	hi5622_ctx.client.name = HI5622_DRV_NAME;
	hi5622_ctx.client.owner = THIS_MODULE;
	hi5622_ctx.client.priv = &hi5622_ctx;
	hi5622_ctx.client.attach = hi5622_client_attach;
	hi5622_ctx.client.detach = hi5622_client_detach;
	hi5622_ctx.client.fw_state = hi5622_client_fw_state;
	hi5622_ctx.client.rx = hi5622_client_rx;

	pr_info(HI5622_DRV_NAME
		": hdpp-style driver skeleton (transport client); allow_device_writes=%d create_netdev=%d alg_protocol=%d\n",
		allow_device_writes, create_netdev, alg_protocol);

	return hi5622_transport_register_client(&hi5622_ctx.client);
}

static void __exit hi5622_drv_exit(void)
{
	hi5622_transport_unregister_client(&hi5622_ctx.client);
	pr_info(HI5622_DRV_NAME ": unloaded\n");
}

module_init(hi5622_drv_init);
module_exit(hi5622_drv_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi5622V100 hdpp-style Wi-Fi driver skeleton (clean-room, Linux 6.18)");
MODULE_AUTHOR("cudy-wr3000v2 wifi-driver-integration lane");
