/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_hdpp.h - hdpp/alg core skeleton of the clean-room Hi5622V100 host
 * driver (the layer that sits on the transport interface and the HCC message
 * layer).
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from the
 * project's decoded contract; no vendor source or object code.
 *
 * Vendor shape this skeleton mirrors (wifi-datapath-slice/REPORT.md,
 * wifi-driver-re 3.3 and wifi-hcc-re 5.5/6.1 [V], task-63 commit 07b9201):
 *
 *   hmac_main_init()  registers chip-space group 1 (28), group 2 (12),
 *                     group 3 (8)
 *   hdpp_main_init()  registers chip-space group 3 (5 ids: 0,4,5,7,8) and the
 *                     per-core tables group 0 (7 ids: 0,1,2,3,4,7,8), and
 *                     registers HCC queue customers for chip queue 2 and core
 *                     queue 0
 *   wal_main_init()   registers chip-space group 2 (19) - merged by id with
 *                     hmac's 12 entries
 *
 *   hdpp_main_init builds the board/chip/band/device/vap/user object tree and
 *   starts the data-path worker; TX MSDUs go through the hmac/hdpp MSDU ring
 *   (NOT the HCC control channel), RX MSDUs are drained from the RX rings.
 *   The hdpp handler ids are pinned now; the MSDU ring format is still [U], so
 *   this skeleton installs the two registration tables with the real ids (stub
 *   bodies) and routes only control-plane HCC frames.
 *
 * Bench/gating contract: hdpp_channel_setup() programs the ETE channel
 * registers (device writes), so it is only reachable when the attended
 * hi5622_hdpp_drv.allow_device_writes=1 gate is open.  Without it the driver
 * attaches, dispatches boot/ready messages and registers the netdev skeleton
 * without writing to the device.
 */
#ifndef _HI5622_HDPP_H
#define _HI5622_HDPP_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct hi5622_chip;
struct sk_buff;
struct net_device;

#define HI5622_HDPP_MAX_BAND	2
#define HI5622_HDPP_MAX_VAP	4
#define HI5622_HDPP_MAX_USER	8

/* hdpp object-tree state. */
#define HI5622_HDPP_DOWN	0
#define HI5622_HDPP_INITED	1
#define HI5622_HDPP_UP		2

/* Placeholder D2H handler ids for the hdpp tables.  The real id->handler maps
 * are [U] (phase25/vocabulary pass pending); the vendor only fixes the table
 * sizes (group 3: 5 entries, core: 7 entries) and the registration call sites
 * (wifi-driver-re 3.3 / phase37 3.2 [V]). */
#define HI5622_HDPP_GROUP3_IDS	5
#define HI5622_HDPP_CORE_IDS	7

/* Opaque vendor object handles: the port must never dereference the vendor
 * layout (it is [U]); only pointers are passed around. */
struct hi5622_hdpp_board;
struct hi5622_hdpp_chip;
struct hi5622_hdpp_band;
struct hi5622_hdpp_device;
struct hi5622_hdpp_vap;
struct hi5622_hdpp_user;

/* One alg configuration descriptor.  The vendor table has 414 entries
 * (wifi-firmware-re 5.9 [V*]) with opaque payloads; the skeleton stores the
 * blobs by id so the sync path can be exercised without the layout. */
#define HI5622_ALG_CFG_MAX	16

struct hi5622_alg_ent {
	u16	id;
	u16	len;
	u8	*data;
};

struct hi5622_alg {
	struct hi5622_chip	*chip;
	spinlock_t		lock;
	u32			count;
	struct hi5622_alg_ent	ents[HI5622_ALG_CFG_MAX];
	u32			sync_seq;
	u32			sync_requests;
	u32			sync_timeouts;
	u32			sync_responses;
	struct completion	*rsp;
	u32			rsp_id;
	bool			enabled;	/* protocol ids [U]: off by default */
};

struct hi5622_hdpp {
	struct hi5622_chip	*chip;
	u32			state;
	u8			chip_id;
	u8			band_id;
	u32			group3_count;
	u32			core_count;
	u32			rx_control;
	u32			rx_frames;
	u32			tx_frames;
	u32			tx_dropped;
	struct hi5622_hdpp_board *board;
	struct hi5622_hdpp_chip	*hchip;
	struct hi5622_hdpp_band	*bands[HI5622_HDPP_MAX_BAND];
	struct hi5622_hdpp_device *devices[HI5622_HDPP_MAX_BAND];
	struct hi5622_hdpp_vap	*vaps[HI5622_HDPP_MAX_BAND][HI5622_HDPP_MAX_VAP];
	struct hi5622_hdpp_user	*users[HI5622_HDPP_MAX_BAND][HI5622_HDPP_MAX_USER];
	struct hi5622_alg	alg;
};

int  hi5622_hdpp_init(struct hi5622_chip *chip);
void hi5622_hdpp_exit(struct hi5622_chip *chip);

/* Program the SR0/DR0 rings and bring the hdpp core up.  Device writes;
 * returns -EPERM when the attended allow_device_writes gate is closed. */
int  hi5622_hdpp_channel_setup(struct hi5622_chip *chip);

/* TX frame from the netdev skeleton.  Copies the frame into an HCC control
 * message (placeholder until the real hdpp TX MSDU ring is reversed) and
 * queues it; frees the skb.
 */
int  hi5622_hdpp_tx_frame(struct hi5622_chip *chip, struct sk_buff *skb);

/* RX entry from the transport: one completed DR-ring payload (an HCC frame).
 * Called from an atomic context; dispatches through the HCC layer.
 */
void hi5622_hdpp_rx_frame(struct hi5622_chip *chip, const void *buf, u32 len);

/* alg config skeleton (own translation unit): table store + the H2D
 * hmac_config_alg_send_event path (group 0 id 2 [V]) and its [U] response. */
int  hi5622_alg_init(struct hi5622_alg *alg, struct hi5622_chip *chip);
void hi5622_alg_exit(struct hi5622_alg *alg);
int  hi5622_alg_cfg_store(struct hi5622_chip *chip, u16 id,
			  const void *data, u32 len);
int  hi5622_alg_cfg_sync(struct hi5622_chip *chip, u16 cfg_id,
			 const void *data, u32 len, u32 timeout_ms);
void hi5622_alg_cfg_response(struct hi5622_chip *chip, u16 cfg_id);
void hi5622_alg_cfg_dump(const struct hi5622_alg *alg, char *buf, size_t len);

/* Minimal object-model accessors (opaque pointers; NULL when out of range). */
struct hi5622_hdpp_band *hi5622_hdpp_band_get(struct hi5622_chip *chip,
					      u8 band_id);
struct hi5622_hdpp_vap *hi5622_hdpp_vap_get(struct hi5622_chip *chip,
					    u8 band_id, u8 vap_id);
const char *hi5622_hdpp_state_str(u32 state);

#endif /* _HI5622_HDPP_H */
