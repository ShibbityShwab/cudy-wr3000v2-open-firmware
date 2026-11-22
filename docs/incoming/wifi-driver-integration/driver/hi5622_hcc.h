/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_hcc.h - HCC (host-chip communication) message layer of the clean-room
 * Hi5622V100 host driver skeleton.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from the
 * project's decoded contract; no vendor source or object code is reproduced.
 *
 * Contract sources (both device-grade [V] decodes):
 *   phase37/THE-PROTOCOL-MAP.md sections 2-3
 *   wifi-hcc-re/REPORT.md sections 3, 5 (task-63; the post-B1 authoritative
 *     re-decode, submodule commit 07b9201)
 *   wifi-driver-re/REPORT.md section 3
 *
 * THE 12-BYTE HCC HEADER (phase37 2.2, re-confirmed hcc-re 3.1 [V]):
 *
 *   +0x00 u8  type/group: low nibble; selects the per-space message map
 *   +0x01 u8  low nibble alloc state (1 = allocated), HIGH NIBBLE = resource
 *             group / source core: hcc_msg_tx copies it into +0x09; the ready
 *             handler reads it back as the source core (2.4G/5G separation)
 *   +0x02 u8  per-message field A
 *   +0x03 u8  per-message field B
 *   +0x04 u16 total length = payload + 12
 *   +0x06 u16 message id (the dispatch key)
 *   +0x08 u8  per-message field C (wal: sequence counter)
 *   +0x09 u8  retry counter
 *   +0x0a u16 SR/ETE transport tag 0x5a5a (SR-layer constant)
 *   +0x0c --  payload start
 *
 * DISPATCH SPACES (hcc-re 6.3 [V]): there are THREE register spaces - type 0
 * = the chip map (plat group 4, hmac groups 1/2/3, wal group 2, hdpp group 3
 * all register there) and types 1/2 = the two per-core maps (only hdpp
 * registers there, group 0).  Within a space the header type nibble indexes a
 * 12-byte group record {count, handler_tab, aux}; the u16 id indexes the
 * group's 16-byte dispatch records {id, func, aux, stats} where the stats word
 * is ignored by the vendor dispatch (hcc-re 5.2).  hcc_msg_register_tab_chip()
 * installs one chip-space group; hcc_msg_register_tab_core() installs the same
 * table into both core spaces (the vendor's customise(1)+customise(2)).
 * Registrations for the same space/group MERGE by id: hmac chip group 3 (8 ids)
 * and hdpp chip group 3 (5 ids) share one table.
 *
 * GROUPS KNOWN SO FAR (hcc-re 5.5 [V]):
 *   chip space, group 4: plat boot/ready/heartbeat/excep, ids 0-4
 *   chip space, group 3: hmac ids 9,10,15,16,17,18,19,20 + hdpp ids 0,4,5,7,8
 *   chip space, group 2: hmac ids 22-29,33-36 + wal ids 0-9,12,13,20,21,30,31,
 *                        32,37,38 (merged by the two registrations)
 *   chip space, group 1: hmac 28 ids (0,1,2,3,5,9,10,11,13,14,15,16,17,18,20,
 *                        21,22,24,25,26,29,31,34,35,36,37,38,39)
 *   core spaces, group 0: hdpp ids 0,1,2,3,4,7,8
 * The hmac/wal tables belong to the hmac/wal port slices; this skeleton
 * installs the group-4 boot table and the two hdpp tables, and the HCC layer
 * merges whatever those future slices register.
 */
#ifndef _HI5622_HCC_H
#define _HI5622_HCC_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct hi5622_chip;

/* 12-byte wire header (phase37 2.2 [V]); access it through the accessors in
 * hi5622_hcc.c, never with unaligned casts. */
#define HI5622_HCC_HDR_LEN		12
#define HI5622_HCC_MAX_PAYLOAD		0x674	/* hcc_msg_alloc cap [V] */
#define HI5622_HCC_MAX_LEN		(HI5622_HCC_HDR_LEN + HI5622_HCC_MAX_PAYLOAD)
#define HI5622_HCC_SR_TRANSPORT_TAG	0x5a5a	/* SR/ETE-layer constant [V] */

/* Register spaces (hcc-re 6.3 [V]). */
#define HI5622_HCC_SPACE_CHIP		0	/* type 0: chip map */
#define HI5622_HCC_SPACE_CORE0		1	/* type 1: core/band 0 map */
#define HI5622_HCC_SPACE_CORE1		2	/* type 2: core/band 1 map */
#define HI5622_HCC_SPACES		3

/* Group indexes inside a space (0..15 encodable; vendor uses 0..4). */
#define HI5622_HCC_GROUP_CFG		0	/* core spaces use 0; chip 2/3 cfg */
#define HI5622_HCC_GROUP_HMAC		1	/* chip space: hmac 28 ids */
#define HI5622_HCC_GROUP_CFG_MIXED	2	/* chip space: hmac 12 + wal 19 */
#define HI5622_HCC_GROUP_HDPP		3	/* chip space: hdpp 5 + hmac 8 */
#define HI5622_HCC_GROUP_CHIP_READY	4	/* chip space: plat boot, 5 ids */
#define HI5622_HCC_GROUPS		5

/* Largest per-group id in the known vendor tables (hmac group 1 id 39). */
#define HI5622_HCC_GROUP_MAX		64

/* Vendor boot-message ids in chip space group 4 (phase37 3.2 / hcc-re 2.4,
 * byte-verified [V]). */
#define HI5622_HCC_BOOT_DEV_LOG		0	/* oam device-log blob */
#define HI5622_HCC_BOOT_PLAT_READY	1	/* releases wait #1 */
#define HI5622_HCC_BOOT_HOST_READY	2	/* releases wait #2 (payload <= 0x63 B) */
#define HI5622_HCC_BOOT_EXCEPTION	3
#define HI5622_HCC_BOOT_HEARTBEAT	4
#define HI5622_HCC_BOOT_COUNT		5

/* Vendor H2D builder ids (phase37 2.3 / hcc-re 6.2 [V]); used by the
 * skeletons. */
#define HI5622_HCC_ID_CFG_SEND_EVENT	1	/* chip group 0: hmac_config_send_event */
#define HI5622_HCC_ID_CFG_ALG_EVENT	2	/* chip group 0: hmac_config_alg_send_event */
#define HI5622_HCC_ID_USER_ADD		7	/* chip group 0: hmac_user_add */
#define HI5622_HCC_ID_USER_ADD_NOTIFY_ALG 8	/* chip group 0: hmac_user_add_notify_alg */
#define HI5622_HCC_ID_USER_DEL_NOTIFY	9	/* chip group 0: hmac_del_user_notify_dmac */

/* Placeholder D2H id for the netdev control-path frame (the real hdpp data
 * path is a dedicated MSDU ring, not HCC [U]). */
#define HI5622_HCC_ID_HDPP_TX_PLACEHOLDER 0x20

/* One decompressed HCC frame as handed to a group handler. */
struct hi5622_hcc_rx {
	const u8	*data;		/* complete frame (12-byte header included) */
	u32		len;		/* frame length as delivered */
	u32		wire_len;	/* le16 at +4 (payload + 12) [V] */
	u8		space;		/* dispatch space (HI5622_HCC_SPACE_*) */
	u8		group;		/* byte0 & 0xf (group inside the space) */
	u8		rgroup;		/* byte1 >> 4 (resource group / source core) */
	u16		id;		/* le16 at +6 */
};

typedef void (*hi5622_hcc_fn)(struct hi5622_chip *chip,
			      const struct hi5622_hcc_rx *rx, void *aux);

/* 12-byte registration record handed to the registration functions.  The
 * vendor's input records are {u16 id, u32 func, u32 aux} at a 12-byte stride
 * (hcc-re 3.2 [V]); rsvd is that stride's padding, kept explicit. */
struct hi5622_hcc_reg {
	u16		id;
	u16		rsvd;
	hi5622_hcc_fn	fn;
	void		*aux;
};

/* 16-byte dispatch record the vendor merges registrations into [V].  The last
 * word is the vendor's stats counter; its dispatch ignores it (hcc-re 5.2). */
struct hi5622_hcc_handler {
	u32		id;
	hi5622_hcc_fn	fn;
	void		*aux;
	u32		stats;
};

/* Per-space/group record {count, handler_tab, aux} (12-byte stride [V]). */
struct hi5622_hcc_group {
	u32		count;
	struct hi5622_hcc_handler *tab;
	void		*aux;
};

/* An H2D message owned by the HCC layer between alloc and tx completion. */
struct hi5622_hcc_msg {
	struct list_head	node;
	u32			len;	/* 12 + payload */
	u8			data[];	/* the complete wire frame */
};

struct hi5622_hcc {
	struct hi5622_chip	*chip;
	spinlock_t		lock;
	struct list_head	txq;
	struct work_struct	tx_work;
	struct hi5622_hcc_group groups[HI5622_HCC_SPACES][HI5622_HCC_GROUPS];
	struct hi5622_hcc_handler
				handlers[HI5622_HCC_SPACES][HI5622_HCC_GROUPS]
					[HI5622_HCC_GROUP_MAX];
	u32			tx_queued;
	u32			tx_sent;
	u32			tx_failed;
	u32			tx_dropped;
	u32			rx_frames;
	u32			rx_dispatched;
	u32			rx_unknown;
	u32			max_frame;	/* transport SR buffer size */
};

int  hi5622_hcc_init(struct hi5622_hcc *hcc, struct hi5622_chip *chip);
void hi5622_hcc_exit(struct hi5622_hcc *hcc);

/* Install a registration table into one explicit chip-space group. */
int  hi5622_hcc_register_tab_chip(struct hi5622_hcc *hcc, u32 group,
				  const struct hi5622_hcc_reg *tab, u32 count);
/* Install the same table into both per-core spaces (vendor customise(1)+
 * customise(2), hcc-re 6.3 [V]). */
int  hi5622_hcc_register_tab_core(struct hi5622_hcc *hcc, u32 group,
				  const struct hi5622_hcc_reg *tab, u32 count);

struct hi5622_hcc_msg *hi5622_hcc_msg_alloc(u32 payload_len, gfp_t gfp);
void hi5622_hcc_msg_free(struct hi5622_hcc_msg *msg);

/**
 * hi5622_hcc_msg_set_hdr() - fill the 12-byte header of a freshly allocated msg
 * @msg: message from hi5622_hcc_msg_alloc()
 * @group: byte +0 type/group low nibble
 * @rgroup: resource group / source core; stored in the HIGH nibble of byte +1
 *          (the vendor hcc_msg_tx copies it into +9 for the queue lookup)
 * @field_a: byte +2 (per message)
 * @field_b: byte +3 (per message)
 * @id: byte +6 message id
 * @field_c: byte +8 (per message; wal uses a sequence counter)
 */
void hi5622_hcc_msg_set_hdr(struct hi5622_hcc_msg *msg, u8 group, u8 rgroup,
			    u8 field_a, u8 field_b, u16 id, u8 field_c);

/* Queue one H2D message; ownership passes to the HCC layer. */
int  hi5622_hcc_tx(struct hi5622_chip *chip, struct hi5622_hcc_msg *msg);

/* Wake the TX drain worker (mailbox D2H id 6/7 semantics [V]). */
void hi5622_hcc_kick(struct hi5622_chip *chip);

/* Drop every queued message (driver teardown). */
void hi5622_hcc_flush(struct hi5622_hcc *hcc);

/* Dispatch one complete D2H HCC frame in the given space (called from the
 * transport rx callback, i.e. atomic context).  hi5622_hcc_process() uses the
 * chip space, which is where the control/boot messages observed so far live
 * (DR0 = control channel [I]); the core spaces are dispatched by the future
 * per-core data-path service.  Returns 0 when a handler ran, -ENOENT when the
 * (group,id) pair has no handler, -EINVAL on a bad frame. */
int  hi5622_hcc_process(struct hi5622_chip *chip, const void *buf, u32 len);
int  hi5622_hcc_process_space(struct hi5622_chip *chip, u32 space,
			      const void *buf, u32 len);

#endif /* _HI5622_HCC_H */
