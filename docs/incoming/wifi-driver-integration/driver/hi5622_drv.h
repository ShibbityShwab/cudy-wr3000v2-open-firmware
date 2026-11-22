/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_drv.h - driver-private glue of the clean-room Hi5622V100
 * hdpp-style Wi-Fi driver skeleton (transport client + HCC + hdpp + netdev).
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64); no
 * vendor source or object code is reproduced.
 *
 * Object model: one struct hi5622_chip per probed transport endpoint (the
 * board exposes two 59e7:0005 endpoints, one per root complex).  Each chip
 * owns its HCC message layer, its D2H dispatch state, the ETE ring-service
 * wrapper, the hdpp/alg core and (optionally) one netdev + wiphy stub.
 */
#ifndef _HI5622_DRV_H
#define _HI5622_DRV_H

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "hi5622_transport_iface.h"
#include "hi5622_hcc.h"
#include "hi5622_hdpp.h"

#define HI5622_DRV_NAME			"hi5622_hdpp_drv"
#define HI5622_DRV_MAX_CHIPS		4
#define HI5622_DRV_READY_TIMEOUT_MS	15000

/* Driver-level state bits (per chip). */
#define HI5622_DRV_STATE_ATTACHED	BIT(0)	/* transport client attached */
#define HI5622_DRV_STATE_TABLES		BIT(1)	/* HCC+hpdpp tables installed */
#define HI5622_DRV_STATE_PLAT_READY	BIT(2)	/* HCC group4 id 1 seen */
#define HI5622_DRV_STATE_DEVICE_READY	BIT(3)	/* HCC group4 id 2 seen */
#define HI5622_DRV_STATE_CHANNELS	BIT(4)	/* ETE rings programmed */
#define HI5622_DRV_STATE_RUNNING	BIT(5)	/* data path nominal */
#define HI5622_DRV_STATE_DETACHING	BIT(6)
#define HI5622_DRV_STATE_FAILED		BIT(7)

struct hi5622_chip;

/* D2H mailbox-layer bookkeeping: ids 0..10 per phase37 3.1 [V]. */
struct hi5622_d2h {
	struct hi5622_chip	*chip;
	spinlock_t		lock;
	u32			count[HI5622_TR_D2H_IDS];
	u32			last_id;
	u32			frames;		/* HCC frames received */
	u32			dropped;	/* frames with no handler / bad */
};

/* ETE ring service wrapper: the transport owns the rings, this counts. */
struct hi5622_ete {
	struct hi5622_chip	*chip;
	bool			inited;
	u32			depth;
	u32			buf_len;
	u32			tx_posts;
	u32			tx_errors;
	u32			rx_payloads;
	u32			rx_errors;
};

struct hi5622_state_notifier {
	struct list_head	node;
	void			(*cb)(struct hi5622_chip *chip, u32 old_state,
				      u32 new_state, void *priv);
	void			*priv;
};

/* One Wi-Fi endpoint / hdpp chip instance. */
struct hi5622_chip {
	struct list_head	node;
	struct device		*dev;
	struct hi5622_transport	*tr;
	const struct hi5622_transport_ops *ops;
	u32			index;
	char			name[24];
	struct workqueue_struct	*wq;
	struct net_device	*netdev;

	struct hi5622_hcc	hcc;
	struct hi5622_d2h	d2h;
	struct hi5622_ete	ete;
	struct hi5622_hdpp	hdpp;

	u32			state;
	spinlock_t		state_lock;
	struct list_head	notifiers;	/* protected by notifier_lock */
	struct mutex		notifier_lock;
	bool			state_work_queued;
	u32			state_old_snapshot;
	struct completion	plat_ready;
	struct completion	device_ready;
	struct work_struct	setup_work;
	struct work_struct	state_work;

	bool			allow_device_writes;

	/* wiphy stub (CONFIG_CFG80211 is not set on this 6.18 build; full
	 * cfg80211 integration is explicitly out of scope for the skeleton). */
	struct {
		bool		present;
		u32		bands;
		u32		ops_planned;
	} wiphy_stub;

	u32			tx_frames;
	u32			tx_dropped;
	u32			rx_frames;
};

/* Module-wide driver context (single transport client). */
struct hi5622_driver_ctx {
	struct list_head	chips;
	struct mutex		lock;
	struct hi5622_transport_client client;
};

/* ---- d2h dispatch (hi5622_d2h.c) ---- */
int  hi5622_d2h_init(struct hi5622_chip *chip);
void hi5622_d2h_exit(struct hi5622_chip *chip);
void hi5622_d2h_rx_frame(struct hi5622_chip *chip, const void *buf, u32 len);
int  hi5622_d2h_register_boot_table(struct hi5622_chip *chip);
u32  hi5622_d2h_count(const struct hi5622_chip *chip, u32 id);

/* ---- ETE ring service (hi5622_ete.c) ---- */
int  hi5622_ete_init(struct hi5622_chip *chip);
void hi5622_ete_exit(struct hi5622_chip *chip);
int  hi5622_ete_tx_hcc(struct hi5622_chip *chip, const void *data, u32 len);
int  hi5622_ete_rx_service(struct hi5622_chip *chip);
bool hi5622_ete_ready(const struct hi5622_chip *chip);

/* ---- chip state / notifications (hi5622_drv_main.c) ---- */
void hi5622_chip_set_state(struct hi5622_chip *chip, u32 new_state,
			   const char *reason);
u32  hi5622_chip_get_state(const struct hi5622_chip *chip);
int  hi5622_chip_state_register(struct hi5622_chip *chip,
				void (*cb)(struct hi5622_chip *, u32, u32,
					   void *), void *priv);
void hi5622_chip_state_unregister(struct hi5622_chip *chip,
				  void (*cb)(struct hi5622_chip *, u32, u32,
					     void *), void *priv);
const char *hi5622_chip_state_str(u32 state, char *buf, size_t len);

/* ---- netdev + wiphy stub (hi5622_netdev.c) ---- */
int  hi5622_netdev_register(struct hi5622_chip *chip);
void hi5622_netdev_sync_state(struct hi5622_chip *chip);
void hi5622_netdev_unregister(struct hi5622_chip *chip);
int  hi5622_wiphy_stub_init(struct hi5622_chip *chip);

#endif /* _HI5622_DRV_H */
