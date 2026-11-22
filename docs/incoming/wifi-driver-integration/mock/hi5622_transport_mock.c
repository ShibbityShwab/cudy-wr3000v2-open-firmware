// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_transport_mock.c - simulated transport for the MOCK build of the
 * clean-room Hi5622V100 hdpp-style driver skeleton.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64).
 *
 * This file implements the transport-side interface (interface/
 * hi5622_transport_iface.h) with no hardware at all: no PCI device, no BAR,
 * no DMA.  It exists so the driver skeleton's attach/boot/dispatch/TX/RX
 * plumbing can be compiled AND exercised as a single self-contained module on
 * any 6.18 test kernel:
 *
 *   insmod hi5622_hdpp_mock.ko allow_device_writes=1
 *   dmesg | grep -E 'hdpp|hcc|mock|d2h|setup'
 *   rmmod hi5622_hdpp_mock
 *
 * The mock simulates a transport that is already firmware-ready: it reports
 * the full state mask, injects the two HCC group-4 boot completes
 * (id 1 plat-ready, id 2 DEVICE READY) exactly as the real device would, and
 * echoes each H2D HCC frame with a heartbeat D2H frame.  It deliberately
 * registers in a "client registers first / mock busy" simulation so the
 * smoke test walks the same callbacks as the real split build.
 *
 * The MOCK TRANSPORT MUST NEVER BE USED ON THE BENCH: it writes nothing, but
 * it also proves nothing about the hardware.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/byteorder/generic.h>
#include <linux/unaligned.h>

#include "hi5622_transport_iface.h"

#define MOCK_NAME		"hi5622_mock"
#define MOCK_HCC_HDR_LEN	12
#define MOCK_HCC_TAG		0x5a5a
#define MOCK_MAX_FRAME		2048

static unsigned int mock_delay_ms = 50;
module_param(mock_delay_ms, uint, 0444);
MODULE_PARM_DESC(mock_delay_ms, "delay between simulated boot/D2H events");

static bool mock_ete_denied;
module_param(mock_ete_denied, bool, 0444);
MODULE_PARM_DESC(mock_ete_denied, "make ops->ete_init fail (simulates a closed write gate)");

struct mock_transport {
	struct hi5622_transport_ops	ops;
	struct hi5622_transport_client	*client;
	void				*priv;
	spinlock_t			lock;
	wait_queue_head_t		wait;
	u32				state;
	u32				d2h_events;
	u32				d2h_count;
	u32				h2d_count;
	u32				rx_count;
	u8				last_h2d[MOCK_MAX_FRAME];
	u32				last_h2d_len;
};

static struct mock_transport *mock;
static struct device *mock_dev;
static struct work_struct mock_boot_work;
static struct work_struct mock_echo_work;

static void mock_build_frame(u8 *buf, u8 group, u8 rgroup, u16 id,
			     const void *payload, u32 plen)
{
	memset(buf, 0, MOCK_HCC_HDR_LEN + plen);
	buf[0] = group & 0x0f;
	buf[1] = (u8)((rgroup << 4) | 0x01);
	put_unaligned_le16((u16)(MOCK_HCC_HDR_LEN + plen), buf + 4);
	put_unaligned_le16(id, buf + 6);
	put_unaligned_le16(MOCK_HCC_TAG, buf + 10);
	if (payload && plen)
		memcpy(buf + MOCK_HCC_HDR_LEN, payload, plen);
}

/* Inject one D2H HCC frame through the client rx() callback (real-path shape:
 * a completed DR-ring payload carrying the 12-byte header + payload). */
static void mock_inject(u8 group, u8 rgroup, u16 id, const void *payload,
			u32 plen)
{
	struct hi5622_transport_client *client;
	void *priv;
	u8 frame[MOCK_HCC_HDR_LEN + 512];

	if (plen > sizeof(frame) - MOCK_HCC_HDR_LEN)
		return;
	mock_build_frame(frame, group, rgroup, id, payload, plen);

	spin_lock(&mock->lock);
	client = mock->client;
	priv = mock->priv;
	mock->rx_count++;
	spin_unlock(&mock->lock);
	if (!client || !client->rx)
		return;
	client->rx((struct hi5622_transport *)mock, frame,
		   MOCK_HCC_HDR_LEN + plen, priv);
}

static void mock_echo_worker(struct work_struct *work)
{
	u8 heartbeat[2] = { 0, 0 };

	dev_info(mock_dev, "mock: echo D2H heartbeat after H2D frame\n");
	mock_inject(4, 0, 4, heartbeat, sizeof(heartbeat));
}

static void mock_boot_worker(struct work_struct *work)
{
	struct hi5622_transport_client *client;
	void *priv;
	u32 ready = HI5622_TR_STATE_PROBED | HI5622_TR_STATE_WINDOWS |
		    HI5622_TR_STATE_FW_LOADED | HI5622_TR_STATE_CPU_RELEASED |
		    HI5622_TR_STATE_GATE_ARMED | HI5622_TR_STATE_PLAT_READY |
		    HI5622_TR_STATE_DEVICE_READY;
	u8 plat[1] = { 0 };
	u8 host[1] = { 0 };

	/* Simulate the firmware reaching DEVICE READY. */
	spin_lock(&mock->lock);
	mock->state = ready;
	spin_unlock(&mock->lock);
	wake_up_all(&mock->wait);

	spin_lock(&mock->lock);
	client = mock->client;
	priv = mock->priv;
	spin_unlock(&mock->lock);
	if (client && client->fw_state)
		client->fw_state((struct hi5622_transport *)mock, ready, 0xff,
				 priv);

	msleep(mock_delay_ms ?: 1);
	/* phase37 3.2 [V]: group 4 id 1 = plat ready, id 2 = DEVICE READY */
	dev_info(mock_dev, "mock: inject HCC group 4 id 1 (plat ready)\n");
	mock_inject(4, 0, 1, plat, sizeof(plat));
	msleep(mock_delay_ms ?: 1);
	dev_info(mock_dev, "mock: inject HCC group 4 id 2 (DEVICE READY)\n");
	mock_inject(4, 0, 2, host, sizeof(host));
}

/* ---- ops: H2D ---- */
static int mock_h2d_send(struct hi5622_transport *t, const void *msg, u32 len)
{
	u8 hdr[MOCK_HCC_HDR_LEN];

	if (!msg || len < MOCK_HCC_HDR_LEN || len > MOCK_MAX_FRAME)
		return -EINVAL;
	memcpy(hdr, msg, MOCK_HCC_HDR_LEN);
	spin_lock(&mock->lock);
	mock->h2d_count++;
	mock->last_h2d_len = len;
	memcpy(mock->last_h2d, msg, len);
	spin_unlock(&mock->lock);

	dev_info_ratelimited(mock_dev,
			     "mock: H2D HCC frame %u B (group %u id %u rgroup %u) accepted\n",
			     len, hdr[0] & 0x0f,
			     get_unaligned_le16(hdr + 6), hdr[1] >> 4);
	schedule_work(&mock_echo_work);
	return 0;
}

static int mock_h2d_kick(struct hi5622_transport *t, u32 mailbox_id)
{
	if (mailbox_id != 3) {
		dev_warn(mock_dev, "mock: refusing H2D mailbox id %u\n",
			 mailbox_id);
		return -EINVAL;
	}
	spin_lock(&mock->lock);
	mock->h2d_count++;
	spin_unlock(&mock->lock);
	return 0;
}

/* ---- ops: D2H ---- */
static int mock_d2h_register(struct hi5622_transport *t, u32 id,
			     hi5622_tr_d2h_fn fn, void *arg)
{
	if (id > HI5622_TR_D2H_MAX_ID)
		return -EINVAL;
	dev_info(mock_dev, "mock: D2H id %u handler registered%s\n", id,
		 fn ? "" : " (NULL)");
	return 0;
}

static void mock_d2h_unregister(struct hi5622_transport *t, u32 id)
{
}

/* ---- ops: ETE ---- */
static int mock_ete_init(struct hi5622_transport *t)
{
	if (mock_ete_denied) {
		dev_warn(mock_dev, "mock: ete_init denied (mock_ete_denied=1)\n");
		return -EPERM;
	}
	dev_info(mock_dev, "mock: ETE rings initialised (simulated depth 32, 2048 B)\n");
	return 0;
}

static int mock_ete_tx(struct hi5622_transport *t, const void *data, u32 len)
{
	return mock_h2d_send(t, data, len);
}

static int mock_ete_rx_service(struct hi5622_transport *t)
{
	return 0;
}

static u32 mock_ete_depth(struct hi5622_transport *t)
{
	return 32;
}

static u32 mock_ete_buf_len(struct hi5622_transport *t)
{
	return 2048;
}

/* ---- ops: state ---- */
static u32 mock_get_state(struct hi5622_transport *t)
{
	return READ_ONCE(mock->state);
}

static int mock_wait_ready(struct hi5622_transport *t, u32 mask, u32 timeout_ms)
{
	long ret;

	ret = wait_event_interruptible_timeout(
		mock->wait, (READ_ONCE(mock->state) & mask) == mask,
		msecs_to_jiffies(timeout_ms));
	if (ret > 0)
		return 0;
	return ret == 0 ? -ETIMEDOUT : -ERESTARTSYS;
}

static u32 mock_d2h_count(struct hi5622_transport *t)
{
	return READ_ONCE(mock->d2h_events);
}

/* ---------------------------------------------------------------------
 * Exported interface implementation (same names as pciet_iface.c)
 * ------------------------------------------------------------------- */
int hi5622_transport_register_client(struct hi5622_transport_client *client)
{
	if (!client || !client->name)
		return -EINVAL;

	if (!mock) {
		mock = kzalloc(sizeof(*mock), GFP_KERNEL);
		if (!mock)
			return -ENOMEM;
		spin_lock_init(&mock->lock);
		init_waitqueue_head(&mock->wait);
		INIT_WORK(&mock_boot_work, mock_boot_worker);
		INIT_WORK(&mock_echo_work, mock_echo_worker);
		mock_dev = root_device_register(MOCK_NAME);
		if (IS_ERR(mock_dev)) {
			int ret = PTR_ERR(mock_dev);

			mock_dev = NULL;
			kfree(mock);
			mock = NULL;
			return ret;
		}
		mock->ops.name = MOCK_NAME;
		mock->ops.dev = mock_dev;
		mock->ops.id = 0;
		mock->ops.h2d_send = mock_h2d_send;
		mock->ops.h2d_kick = mock_h2d_kick;
		mock->ops.d2h_register = mock_d2h_register;
		mock->ops.d2h_unregister = mock_d2h_unregister;
		mock->ops.ete_init = mock_ete_init;
		mock->ops.ete_tx = mock_ete_tx;
		mock->ops.ete_rx_service = mock_ete_rx_service;
		mock->ops.ete_depth = mock_ete_depth;
		mock->ops.ete_buf_len = mock_ete_buf_len;
		mock->ops.get_state = mock_get_state;
		mock->ops.wait_ready = mock_wait_ready;
		mock->ops.d2h_count = mock_d2h_count;
		mock->state = HI5622_TR_STATE_PROBED;
	}

	spin_lock(&mock->lock);
	mock->client = client;
	mock->priv = client->priv;
	spin_unlock(&mock->lock);

	dev_info(mock_dev, "mock: client %s registering (simulated transport)\n",
		 client->name);
	client->attach((struct hi5622_transport *)mock, &mock->ops,
		       client->priv);

	/* The simulated firmware was already booted: report ready + schedule
	 * the boot D2H injections through the normal rx() callback path. */
	spin_lock(&mock->lock);
	mock->state |= HI5622_TR_STATE_PLAT_READY |
		       HI5622_TR_STATE_DEVICE_READY;
	spin_unlock(&mock->lock);
	dev_info(mock_dev, "mock: injecting simulated DEVICE READY\n");
	schedule_work(&mock_boot_work);
	return 0;
}

void hi5622_transport_unregister_client(struct hi5622_transport_client *client)
{
	if (!mock || mock->client != client)
		return;

	cancel_work_sync(&mock_boot_work);
	cancel_work_sync(&mock_echo_work);
	if (client->detach)
		client->detach((struct hi5622_transport *)mock, client->priv);

	mock->client = NULL;
	mock->priv = NULL;
	if (mock_dev) {
		root_device_unregister(mock_dev);
		mock_dev = NULL;
	}
	kfree(mock);
	mock = NULL;
}
