// SPDX-License-Identifier: GPL-2.0
/*
 * pciet_iface.c - transport-side implementation of hi5622_transport_iface.h.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from
 * the project's decoded interface contract; no vendor source or object code.
 *
 * This file is added to the hi5622_pciet module by the marked delta hunks in
 * ../transport-delta/0001-pciet-integration-iface.patch.  It turns the
 * transport skeleton's internal state into the client/ops contract that a
 * hdpp-style driver binds to:
 *
 *   - exports hi5622_transport_register_client()/_unregister_client();
 *   - hands the driver a per-instance ops table (HCC H2D send, D2H dispatch,
 *     ETE SR/DR service, firmware state/ready waits);
 *   - performs the phase37 1.2 mailbox D2H service routine (ack, read+drain,
 *     re-arm) when a driver is attached, then dispatches the set bits
 *     lowest-first through the transport's own boot handlers and the driver's
 *     registered handlers;
 *   - fans DR-ring payloads out to the driver's rx() callback.
 *
 * Safety: with no driver attached the transport behaves exactly as before
 * (this file's scan hook returns false and the legacy pciet_d2h_scan path
 * runs; the RX hook returns false and the legacy hexdump stays).  This file
 * performs no device writes until a client is attached and only the fixed
 * D2H ack/re-arm words of phase37 1.2 are written; the never-write register
 * CA 0x400392f0 is not touched (see BUILD_BUG_ON below).
 */
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>

#include "hi5622_pciet.h"
#include "hi5622_transport_iface.h"

/*
 * [driver-port integration delta] attended-bench gate.  With driver_mode=0
 * (the default) the transport keeps its original write behaviour: the D2H scan
 * path stays the legacy read/drain/dispatch and DR payloads keep the legacy
 * logging; an attached driver only observes.  Setting driver_mode=1 moves the
 * D2H service routine (ack CA 0x40101438, clear, re-arm CA 0x40101414) and the
 * dispatch to the iface, exactly as phase37 1.2 specifies.  This split keeps
 * "probe writes nothing" true even if the driver is loaded by mistake: the
 * attended B2 session enables the protocol as an explicit step.
 */
static bool driver_mode;
module_param(driver_mode, bool, 0644);
MODULE_PARM_DESC(driver_mode,
		 "hand the D2H service routine + dispatch to an attached hdpp-style driver (attended bench step; default off)");

/* The D2H service words of phase37 1.2 [V]; both live in the IO window and
 * are NOT the never-write register (asserted below). */
#define HI5622_IFACE_CA_D2H_ACK		0x40101438u	/* out[3], write 1 */
#define HI5622_IFACE_CA_D2H_REARM	0x40101414u	/* out[4], write 1 */

#ifndef MODULE
#error "pciet_iface.c is a kernel-module companion, not builtin code"
#endif

struct hi5622_tr_hook {
	hi5622_tr_d2h_fn	fn;
	void			*arg;
};

struct hi5622_transport {
	struct list_head	node;
	struct hi5622_pci	*ctx;
	struct hi5622_transport_ops ops;

	struct mutex		io_lock;	/* serialises ETE SR/DR service */
	spinlock_t		lock;		/* client binding + hooks + state */
	struct hi5622_transport_client *client;
	bool			client_ready;
	void			*client_priv;
	struct hi5622_tr_hook	d2h[HI5622_TR_D2H_IDS];
	void			(*boot_d2h[HI5622_TR_D2H_IDS])(struct hi5622_pci *ctx,
								u32 id);
	atomic_t		cb_active;	/* in-flight client callbacks */
	wait_queue_head_t	cb_wait;
	atomic_t		refs;		/* list lookup references */
	wait_queue_head_t	ref_wait;
	wait_queue_head_t	wait;		/* ops->wait_ready sleepers */
	u32			state;
	u32			d2h_events;
	u32			seq;
};

/* One registered driver.  The client struct itself is owned by the driver, so
 * the iface keeps a wrapper for its list node. */
struct hi5622_iface_client {
	struct list_head		node;
	struct hi5622_transport_client	*client;
};

/* forward declarations: the ops vtable is filled before the transport is
 * published, the client attach/detach helpers are used by the lifecycle
 * hooks below. */
static int iface_h2d_send(struct hi5622_transport *t, const void *msg, u32 len);
static int iface_h2d_kick(struct hi5622_transport *t, u32 mailbox_id);
static int iface_d2h_register(struct hi5622_transport *t, u32 id,
			      hi5622_tr_d2h_fn fn, void *arg);
static void iface_d2h_unregister(struct hi5622_transport *t, u32 id);
static int iface_ete_init(struct hi5622_transport *t);
static int iface_ete_tx(struct hi5622_transport *t, const void *data, u32 len);
static int iface_ete_rx_service(struct hi5622_transport *t);
static u32 iface_ete_depth(struct hi5622_transport *t);
static u32 iface_ete_buf_len(struct hi5622_transport *t);
static u32 iface_get_state(struct hi5622_transport *t);
static int iface_wait_ready(struct hi5622_transport *t, u32 state_mask,
			    u32 timeout_ms);
static u32 iface_d2h_count(struct hi5622_transport *t);
static void iface_attach_client(struct hi5622_transport *t,
				struct hi5622_transport_client *c);
static void iface_detach_client(struct hi5622_transport *t);

static LIST_HEAD(hi5622_transports);
static LIST_HEAD(hi5622_clients);
static DEFINE_MUTEX(hi5622_iface_mutex);
static DEFINE_SPINLOCK(hi5622_list_lock);
static u32 hi5622_transport_seq;

static void iface_assert_contract(void)
{
	BUILD_BUG_ON(HI5622_IFACE_CA_D2H_ACK == HI5622_CA_NEVER_WRITE);
	BUILD_BUG_ON(HI5622_IFACE_CA_D2H_REARM == HI5622_CA_NEVER_WRITE);
}

/* ---------------------------------------------------------------------
 * State snapshot
 * ------------------------------------------------------------------- */
static u32 iface_compute_state(struct hi5622_pci *ctx)
{
	u32 state = HI5622_TR_STATE_PROBED;

	if (ctx->windows_programmed)
		state |= HI5622_TR_STATE_WINDOWS;
	if (ctx->fw_loaded)
		state |= HI5622_TR_STATE_FW_LOADED;
	if (ctx->cpu_released)
		state |= HI5622_TR_STATE_CPU_RELEASED;
	if (ctx->gate_announced)
		state |= HI5622_TR_STATE_GATE_ARMED;
	if (completion_done(&ctx->plat_ready))
		state |= HI5622_TR_STATE_PLAT_READY;
	if (completion_done(&ctx->host_ready))
		state |= HI5622_TR_STATE_DEVICE_READY;
	return state & HI5622_TR_STATE_ALL;
}

const char *hi5622_tr_state_str(u32 state, char *buf, size_t len)
{
	static const char *const names[] = {
		"probed", "windows", "fw", "released", "gate-arm",
		"plat-ready", "device-ready",
	};
	size_t pos = 0;
	unsigned int i;

	buf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		if (!(state & BIT(i)))
			continue;
		pos += scnprintf(buf + pos, len - pos, "%s%s",
				 pos ? "|" : "", names[i]);
		if (pos >= len)
			break;
	}
	if (!pos)
		scnprintf(buf, len, "none");
	return buf;
}

/* Find the transport instance for a pci context and take a reference.
 * Every caller must pair it with iface_put(); transport removal drops the
 * list entry, then waits for the reference count to reach zero before the
 * instance is detached and freed (no use-after-free against a concurrent poll
 * thread or IRQ scan). */
static struct hi5622_transport *iface_find(struct hi5622_pci *ctx)
{
	struct hi5622_transport *t;

	spin_lock(&hi5622_list_lock);
	list_for_each_entry(t, &hi5622_transports, node) {
		if (t->ctx == ctx) {
			atomic_inc(&t->refs);
			spin_unlock(&hi5622_list_lock);
			return t;
		}
	}
	spin_unlock(&hi5622_list_lock);
	return NULL;
}

static void iface_put(struct hi5622_transport *t)
{
	if (atomic_dec_and_test(&t->refs))
		wake_up_all(&t->ref_wait);
}

/* ---------------------------------------------------------------------
 * Client callback plumbing
 *
 * Every client callback is wrapped by a cb_active reference taken under
 * t->lock.  unregister/remove clears the binding under the lock and then
 * waits for cb_active to reach zero, which is what makes it safe for the
 * driver module to unload or detach its private state.  The callbacks
 * themselves run without any iface lock held.
 * ------------------------------------------------------------------- */
static bool iface_cb_enter(struct hi5622_transport *t)
{
	unsigned long flags;
	bool ok;

	spin_lock_irqsave(&t->lock, flags);
	ok = t->client && t->client_ready;
	if (ok)
		atomic_inc(&t->cb_active);
	spin_unlock_irqrestore(&t->lock, flags);
	return ok;
}

static void iface_cb_exit(struct hi5622_transport *t)
{
	unsigned long flags;

	spin_lock_irqsave(&t->lock, flags);
	if (atomic_dec_and_test(&t->cb_active))
		wake_up_all(&t->cb_wait);
	spin_unlock_irqrestore(&t->lock, flags);
}

static void iface_wait_quiescent(struct hi5622_transport *t)
{
	wait_event(t->cb_wait, atomic_read(&t->cb_active) == 0);
}

static void iface_notify_state(struct hi5622_transport *t, u32 state,
			       u32 d2h_id)
{
	struct hi5622_transport_client *c;
	void *priv;

	spin_lock(&t->lock);
	c = t->client;
	priv = t->client_priv;
	spin_unlock(&t->lock);
	if (!c || !c->fw_state)
		return;
	c->fw_state(t, state, d2h_id, priv);
}

/* Dispatch one mailbox D2H id: the transport's own boot handler first (so the
 * legacy ready completions and the DR drain still run), then the driver's
 * handler.  Called with a cb_active reference held by the caller. */
static void iface_dispatch_one(struct hi5622_transport *t, u32 id)
{
	struct hi5622_tr_hook hook;
	void (*boot)(struct hi5622_pci *, u32) = NULL;
	struct hi5622_pci *ctx = t->ctx;

	hook = (struct hi5622_tr_hook){ };
	spin_lock(&t->lock);
	if (id < HI5622_TR_D2H_IDS) {
		hook = t->d2h[id];
		boot = t->boot_d2h[id];
	}
	spin_unlock(&t->lock);

	if (boot)
		boot(ctx, id);
	if (hook.fn)
		hook.fn(t, id, hook.arg);
}

/* ---------------------------------------------------------------------
 * Transport-side hooks called from the marked delta hunks
 * ------------------------------------------------------------------- */

/**
 * pciet_iface_d2h_client_scan() - phase37 1.2 mailbox D2H service routine
 * @ctx: transport context
 *
 * Returns true when a driver is attached and this function has performed the
 * whole service for this scan (the legacy transport path must be skipped),
 * false when no driver is attached.
 *
 * The routine deliberately reads the bitmap once BEFORE writing anything: an
 * idle poll cycle must not touch the ack/re-arm words.  Only when a bit is
 * pending does it perform the fixed order of phase37 1.2: ack, read+clear,
 * re-arm, then dispatch lowest-id-first.
 */
bool pciet_iface_d2h_client_scan(struct hi5622_pci *ctx)
{
	struct hi5622_transport *t = iface_find(ctx);
	u32 bitmap, bitmap2, id, last_id = 0;
	bool claimed;

	if (!t)
		return false;
	if (!READ_ONCE(driver_mode)) {
		iface_put(t);
		return false;
	}
	if (!iface_cb_enter(t)) {
		iface_put(t);
		return false;
	}

	bitmap = readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP);
	if (bitmap) {
		/* phase37 1.2 [V]: ack 0x40101438 = 1, read+clear out[1],
		 * re-arm 0x40101414 = 1.  The transport's never-write gate
		 * (CA 0x400392f0) is untouched. */
		writel(1, ctx->bar0 + HI5622_BAR0_IO(HI5622_IFACE_CA_D2H_ACK));
		bitmap2 = readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP);
		writel(0, ctx->bar0 + HI5622_BAR0_D2H_BITMAP);
		writel(1, ctx->bar0 + HI5622_BAR0_IO(HI5622_IFACE_CA_D2H_REARM));
		bitmap |= bitmap2;
		t->d2h_events++;

		dev_dbg(&ctx->pdev->dev, "iface: D2H bitmap 0x%08x (service)\n",
			bitmap);
		for (id = 0; id <= HI5622_TR_D2H_MAX_ID; id++) {
			if (!(bitmap & BIT(id)))
				continue;
			last_id = id;
			iface_dispatch_one(t, id);
		}

		/* Refresh the exported state; fw_state() must see transitions. */
		{
			unsigned long flags;
			u32 state = iface_compute_state(ctx);
			bool changed;

			spin_lock_irqsave(&t->lock, flags);
			changed = state != t->state;
			t->state = state;
			spin_unlock_irqrestore(&t->lock, flags);
			if (changed) {
				wake_up_all(&t->wait);
				iface_notify_state(t, state, last_id);
			}
		}
	}

	claimed = true;
	iface_cb_exit(t);
	iface_put(t);
	return claimed;
}

/**
 * pciet_iface_deliver_rx() - deliver one completed DR-ring payload
 *
 * Returns true when a driver rx() consumed it (transport logging skipped).
 */
bool pciet_iface_deliver_rx(struct hi5622_pci *ctx, const void *buf, u32 len)
{
	struct hi5622_transport *t = iface_find(ctx);
	struct hi5622_transport_client *c;
	void *priv;
	bool ok;

	if (!t)
		return false;
	if (!READ_ONCE(driver_mode)) {
		iface_put(t);
		return false;
	}
	if (!iface_cb_enter(t)) {
		iface_put(t);
		return false;
	}

	spin_lock(&t->lock);
	c = t->client;
	priv = t->client_priv;
	spin_unlock(&t->lock);

	ok = c && c->rx;
	if (ok)
		c->rx(t, buf, len, priv);

	iface_cb_exit(t);
	iface_put(t);
	return ok;
}

/**
 * pciet_iface_transport_added() - publish a freshly probed endpoint
 * @ctx: transport context (fully set up, poll thread may already run)
 */
void pciet_iface_transport_added(struct hi5622_pci *ctx)
{
	struct hi5622_iface_client *ic;
	struct hi5622_transport *t;
	unsigned long flags;

	iface_assert_contract();

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return;

	t->ctx = ctx;
	mutex_init(&t->io_lock);
	spin_lock_init(&t->lock);
	atomic_set(&t->cb_active, 0);
	atomic_set(&t->refs, 1);	/* owner reference held by the list */
	init_waitqueue_head(&t->cb_wait);
	init_waitqueue_head(&t->ref_wait);
	init_waitqueue_head(&t->wait);
	t->state = iface_compute_state(ctx);

	/* Publish the complete ops vtable before the transport can be found by
	 * hi5622_transport_register_client(). */
	t->ops.name = "hi5622_pciet";
	t->ops.dev = &ctx->pdev->dev;
	t->ops.h2d_send = iface_h2d_send;
	t->ops.h2d_kick = iface_h2d_kick;
	t->ops.d2h_register = iface_d2h_register;
	t->ops.d2h_unregister = iface_d2h_unregister;
	t->ops.ete_init = iface_ete_init;
	t->ops.ete_tx = iface_ete_tx;
	t->ops.ete_rx_service = iface_ete_rx_service;
	t->ops.ete_depth = iface_ete_depth;
	t->ops.ete_buf_len = iface_ete_buf_len;
	t->ops.get_state = iface_get_state;
	t->ops.wait_ready = iface_wait_ready;
	t->ops.d2h_count = iface_d2h_count;

	/* Snapshot the transport's own boot handlers (ids 1/2/3 today) before
	 * the driver can register anything: they must keep running. */
	spin_lock_irqsave(&t->lock, flags);
	{
		u32 i;

		for (i = 0; i < HI5622_TR_D2H_IDS; i++)
			t->boot_d2h[i] = ctx->d2h[i].func;
	}
	spin_unlock_irqrestore(&t->lock, flags);

	mutex_lock(&hi5622_iface_mutex);
	spin_lock_irqsave(&hi5622_list_lock, flags);
	t->seq = hi5622_transport_seq++;
	t->ops.id = t->seq;
	list_add_tail(&t->node, &hi5622_transports);
	spin_unlock_irqrestore(&hi5622_list_lock, flags);
	mutex_unlock(&hi5622_iface_mutex);

	/* Attach every registered client; their attach() may call the ops. */
	mutex_lock(&hi5622_iface_mutex);
	list_for_each_entry(ic, &hi5622_clients, node)
		iface_attach_client(t, ic->client);
	mutex_unlock(&hi5622_iface_mutex);
}

/**
 * pciet_iface_transport_removed() - withdraw an endpoint
 * @ctx: transport context about to be torn down
 */
void pciet_iface_transport_removed(struct hi5622_pci *ctx)
{
	struct hi5622_transport *t = NULL, *cur;
	unsigned long flags;

	mutex_lock(&hi5622_iface_mutex);
	spin_lock_irqsave(&hi5622_list_lock, flags);
	list_for_each_entry(cur, &hi5622_transports, node) {
		if (cur->ctx == ctx) {
			t = cur;
			list_del(&t->node);
			break;
		}
	}
	spin_unlock_irqrestore(&hi5622_list_lock, flags);
	if (!t) {
		mutex_unlock(&hi5622_iface_mutex);
		return;
	}

	/* Drop the owner reference and wait for any in-flight iface_find()
	 * user (poll thread / IRQ scan / rx fan-out) to finish.  No new
	 * lookup can succeed: the list entry is gone. */
	iface_put(t);
	wait_event(t->ref_wait, atomic_read(&t->refs) == 0);

	iface_detach_client(t);
	mutex_unlock(&hi5622_iface_mutex);

	kfree(t);
}

/* ---------------------------------------------------------------------
 * Ops implemented for the driver
 * ------------------------------------------------------------------- */
static int iface_h2d_send(struct hi5622_transport *t, const void *msg, u32 len)
{
	struct hi5622_pci *ctx = t->ctx;
	int ret;

	if (!msg || len < 12)
		return -EINVAL;
	if (len > ctx->sr0.buf_len) {
		dev_err_ratelimited(&ctx->pdev->dev,
				    "iface: HCC frame %u B > SR buffer %u B\n",
				    len, ctx->sr0.buf_len);
		return -EMSGSIZE;
	}

	mutex_lock(&t->io_lock);
	ret = pciet_ete_sr_send(ctx, &ctx->sr0, msg, len);
	mutex_unlock(&t->io_lock);
	if (ret)
		return ret;

	return pciet_msg_send(ctx, HI5622_MSG_ETE_DONE);
}

static int iface_h2d_kick(struct hi5622_transport *t, u32 mailbox_id)
{
	struct hi5622_pci *ctx = t->ctx;

	/* Only the ETE-transfer-done announce exists on the H2D success path.
	 * The device-side H2D table registers {1,3,5,6} and id 5 is a BUG()
	 * trap on this firmware, so nothing else is accepted here. */
	if (mailbox_id != HI5622_MSG_ETE_DONE) {
		dev_warn_ratelimited(&ctx->pdev->dev,
				     "iface: refusing H2D mailbox id %u (only 3)\n",
				     mailbox_id);
		return -EINVAL;
	}
	return pciet_msg_send(ctx, mailbox_id);
}

static int iface_d2h_register(struct hi5622_transport *t, u32 id,
			      hi5622_tr_d2h_fn fn, void *arg)
{
	unsigned long flags;
	int ret = 0;

	if (id > HI5622_TR_D2H_MAX_ID)
		return -EINVAL;

	spin_lock_irqsave(&t->lock, flags);
	if (!t->client)
		ret = -ENOTCONN;
	else if (t->d2h[id].fn)
		ret = -EBUSY;
	else {
		t->d2h[id].fn = fn;
		t->d2h[id].arg = arg;
	}
	spin_unlock_irqrestore(&t->lock, flags);
	return ret;
}

static void iface_d2h_unregister(struct hi5622_transport *t, u32 id)
{
	unsigned long flags;

	if (id > HI5622_TR_D2H_MAX_ID)
		return;
	spin_lock_irqsave(&t->lock, flags);
	t->d2h[id].fn = NULL;
	t->d2h[id].arg = NULL;
	spin_unlock_irqrestore(&t->lock, flags);
}

static int iface_ete_init(struct hi5622_transport *t)
{
	struct hi5622_pci *ctx = t->ctx;
	int ret;

	if (!t->client)
		return -ENOTCONN;

	mutex_lock(&t->io_lock);
	ret = pciet_ete_ring_init(ctx, &ctx->sr0, true);
	if (!ret)
		ret = pciet_ete_ring_init(ctx, &ctx->dr0, false);
	mutex_unlock(&t->io_lock);

	if (ret)
		dev_err(&ctx->pdev->dev, "iface: ETE ring init failed: %d\n",
			ret);
	return ret;
}

static int iface_ete_tx(struct hi5622_transport *t, const void *data, u32 len)
{
	struct hi5622_pci *ctx = t->ctx;
	int ret;

	if (!data || !len || len > ctx->sr0.buf_len)
		return -EINVAL;
	mutex_lock(&t->io_lock);
	ret = pciet_ete_sr_send(ctx, &ctx->sr0, data, len);
	mutex_unlock(&t->io_lock);
	return ret;
}

static void iface_dr_cb(struct hi5622_pci *ctx, void *va, u32 len)
{
	if (!pciet_iface_deliver_rx(ctx, va, len))
		dev_dbg_ratelimited(&ctx->pdev->dev,
				    "iface: DR payload %u B with no client\n",
				    len);
}

static int iface_ete_rx_service(struct hi5622_transport *t)
{
	struct hi5622_pci *ctx = t->ctx;
	int n;

	mutex_lock(&t->io_lock);
	n = pciet_ete_dr_consume(ctx, &ctx->dr0, iface_dr_cb);
	mutex_unlock(&t->io_lock);
	return n;
}

static u32 iface_ete_depth(struct hi5622_transport *t)
{
	return t->ctx->sr0.depth;
}

static u32 iface_ete_buf_len(struct hi5622_transport *t)
{
	return t->ctx->sr0.buf_len;
}

static u32 iface_get_state(struct hi5622_transport *t)
{
	return iface_compute_state(t->ctx);
}

static int iface_wait_ready(struct hi5622_transport *t, u32 state_mask,
			    u32 timeout_ms)
{
	long ret;

	ret = wait_event_interruptible_timeout(
		t->wait,
		(iface_compute_state(t->ctx) & state_mask) == state_mask,
		msecs_to_jiffies(timeout_ms));
	if (ret > 0)
		return 0;
	return ret == 0 ? -ETIMEDOUT : -ERESTARTSYS;
}

static u32 iface_d2h_count(struct hi5622_transport *t)
{
	return t->d2h_events;
}

/* ---------------------------------------------------------------------
 * Attach / detach plumbing
 * ------------------------------------------------------------------- */
static void iface_attach_client(struct hi5622_transport *t,
				struct hi5622_transport_client *c)
{
	unsigned long flags;

	spin_lock_irqsave(&t->lock, flags);
	if (t->client) {
		spin_unlock_irqrestore(&t->lock, flags);
		dev_warn_ratelimited(t->ops.dev,
				     "iface: transport already has client %s\n",
				     t->client->name ?: "?");
		return;
	}
	t->client = c;
	t->client_priv = c->priv;
	t->state = iface_compute_state(t->ctx);
	spin_unlock_irqrestore(&t->lock, flags);

	c->attach(t, &t->ops, c->priv);

	spin_lock_irqsave(&t->lock, flags);
	t->client_ready = true;
	t->state = iface_compute_state(t->ctx);
	spin_unlock_irqrestore(&t->lock, flags);

	if (c->fw_state)
		c->fw_state(t, t->state, 0xff, c->priv);
	dev_info(t->ops.dev, "iface: client %s attached (transport %u)\n",
		 c->name ?: "?", t->ops.id);
}

static void iface_detach_client(struct hi5622_transport *t)
{
	struct hi5622_transport_client *c;
	void *priv;
	unsigned long flags;
	u32 i;

	spin_lock_irqsave(&t->lock, flags);
	c = t->client;
	priv = t->client_priv;
	t->client = NULL;
	t->client_ready = false;
	for (i = 0; i < HI5622_TR_D2H_IDS; i++) {
		t->d2h[i].fn = NULL;
		t->d2h[i].arg = NULL;
	}
	spin_unlock_irqrestore(&t->lock, flags);

	iface_wait_quiescent(t);
	wake_up_all(&t->wait);

	if (c) {
		dev_info(t->ops.dev, "iface: client %s detached\n",
			 c->name ?: "?");
		if (c->detach)
			c->detach(t, priv);
	}
}

/* ---------------------------------------------------------------------
 * Client registration (exported to the driver module)
 * ------------------------------------------------------------------- */
int hi5622_transport_register_client(struct hi5622_transport_client *client)
{
	struct hi5622_iface_client *ic;
	struct hi5622_transport *t;

	if (!client || !client->name)
		return -EINVAL;

	ic = kzalloc(sizeof(*ic), GFP_KERNEL);
	if (!ic)
		return -ENOMEM;
	ic->client = client;

	mutex_lock(&hi5622_iface_mutex);
	list_add_tail(&ic->node, &hi5622_clients);
	list_for_each_entry(t, &hi5622_transports, node)
		iface_attach_client(t, client);
	mutex_unlock(&hi5622_iface_mutex);

	pr_info("hi5622_pciet: client %s registered\n", client->name);
	return 0;
}
EXPORT_SYMBOL_GPL(hi5622_transport_register_client);

void hi5622_transport_unregister_client(struct hi5622_transport_client *client)
{
	struct hi5622_iface_client *ic;
	struct hi5622_transport *t;

	if (!client)
		return;

	mutex_lock(&hi5622_iface_mutex);
	list_for_each_entry(ic, &hi5622_clients, node) {
		if (ic->client != client)
			continue;
		list_del(&ic->node);
		kfree(ic);
		break;
	}
	list_for_each_entry(t, &hi5622_transports, node)
		if (t->client == client)
			iface_detach_client(t);
	mutex_unlock(&hi5622_iface_mutex);

	pr_info("hi5622_pciet: client %s unregistered\n", client->name);
}
EXPORT_SYMBOL_GPL(hi5622_transport_unregister_client);
