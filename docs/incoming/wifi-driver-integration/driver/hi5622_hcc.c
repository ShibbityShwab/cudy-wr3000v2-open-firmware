// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_hcc.c - HCC message layer of the clean-room Hi5622V100 host driver
 * skeleton: TX queue -> ETE SR service, and the D2H (space,group,id) dispatch
 * tables.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from
 * phase37/THE-PROTOCOL-MAP.md sections 2-3, wifi-hcc-re/REPORT.md sections
 * 3/5 (task-63, submodule commit 07b9201) and wifi-driver-re section 3.
 * No vendor source or object code.
 */
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>
#include <linux/unaligned.h>

#include "hi5622_drv.h"

/* ---------------------------------------------------------------------
 * Header accessors (12-byte wire form, phase37 2.2 / hcc-re 3.1 [V])
 * ------------------------------------------------------------------- */
static void hcc_hdr_store(struct hi5622_hcc_msg *msg, u8 group, u8 rgroup,
			  u8 field_a, u8 field_b, u16 id, u8 field_c)
{
	u8 *p = msg->data;

	p[0] = group & 0x0f;
	/* low nibble: alloc state (1 = allocated); high nibble: resource
	 * group / source core (hcc_msg_tx copies it into +9) [V] */
	p[1] = (u8)((rgroup << 4) | 0x01);
	p[2] = field_a;
	p[3] = field_b;
	put_unaligned_le16((u16)msg->len, p + 4);
	put_unaligned_le16(id, p + 6);
	p[8] = field_c;
	p[9] = 0;	/* retry counter [V] */
	put_unaligned_le16(HI5622_HCC_SR_TRANSPORT_TAG, p + 10);
}

/* ---------------------------------------------------------------------
 * TX drain: queued HCC frames -> transport SR0 node + id-3 announce
 * ------------------------------------------------------------------- */
static void hi5622_hcc_tx_worker(struct work_struct *work)
{
	struct hi5622_hcc *hcc = container_of(work, struct hi5622_hcc, tx_work);
	struct hi5622_chip *chip = hcc->chip;
	unsigned long flags;
	unsigned int budget = 32;

	for (;;) {
		struct hi5622_hcc_msg *msg;
		int ret;

		spin_lock_irqsave(&hcc->lock, flags);
		msg = list_first_entry_or_null(&hcc->txq, struct hi5622_hcc_msg,
					       node);
		if (msg)
			list_del_init(&msg->node);
		spin_unlock_irqrestore(&hcc->lock, flags);
		if (!msg || !budget--)
			break;

		/* The transport path is one SR0 node + mailbox announce id 3
		 * (phase37 2.1 [V], B2-proven).  pciet_msg_send returns
		 * -ETIMEDOUT on the vendor 0x8b2d-style busy condition
		 * (hcc-re 3.4/5.4); count it as a retryable tx failure. */
		ret = hi5622_ete_tx_hcc(chip, msg->data, msg->len);
		if (ret) {
			hcc->tx_failed++;
			dev_dbg_ratelimited(chip->dev,
					    "hcc: tx %u B failed: %d\n",
					    msg->len, ret);
		} else {
			hcc->tx_sent++;
		}
		hi5622_hcc_msg_free(msg);
	}

	/* A producer may have raced the empty check above. */
	spin_lock_irqsave(&hcc->lock, flags);
	if (!list_empty(&hcc->txq))
		queue_work(chip->wq, &hcc->tx_work);
	spin_unlock_irqrestore(&hcc->lock, flags);
}

/* ---------------------------------------------------------------------
 * Lifecycle / registration
 * ------------------------------------------------------------------- */
/* Wire-layout asserts: the sizes below are the decoded vendor strides
 * (12-byte registration input, 16-byte dispatch, 12-byte group record)
 * (driver-re 3.2 / hcc-re 3.2 [V]). */
static void hi5622_hcc_assert_layout(void)
{
	BUILD_BUG_ON(HI5622_HCC_HDR_LEN != 12);
	BUILD_BUG_ON(sizeof(struct hi5622_hcc_reg) != 12);
	BUILD_BUG_ON(sizeof(struct hi5622_hcc_handler) != 16);
	BUILD_BUG_ON(sizeof(struct hi5622_hcc_group) != 12);
	BUILD_BUG_ON(sizeof(struct hi5622_hcc_rx) > 32);
}

int hi5622_hcc_init(struct hi5622_hcc *hcc, struct hi5622_chip *chip)
{
	u32 s, g;

	hi5622_hcc_assert_layout();
	hcc->chip = chip;
	spin_lock_init(&hcc->lock);
	INIT_LIST_HEAD(&hcc->txq);
	INIT_WORK(&hcc->tx_work, hi5622_hcc_tx_worker);
	hcc->max_frame = HI5622_HCC_MAX_LEN;

	for (s = 0; s < HI5622_HCC_SPACES; s++)
		for (g = 0; g < HI5622_HCC_GROUPS; g++)
			hcc->groups[s][g].tab = hcc->handlers[s][g];
	return 0;
}

void hi5622_hcc_flush(struct hi5622_hcc *hcc)
{
	struct hi5622_hcc_msg *msg, *tmp;
	unsigned long flags;

	cancel_work_sync(&hcc->tx_work);
	spin_lock_irqsave(&hcc->lock, flags);
	list_for_each_entry_safe(msg, tmp, &hcc->txq, node) {
		list_del_init(&msg->node);
		hi5622_hcc_msg_free(msg);
	}
	spin_unlock_irqrestore(&hcc->lock, flags);
}

void hi5622_hcc_exit(struct hi5622_hcc *hcc)
{
	hi5622_hcc_flush(hcc);
}

/* Merge one registration table into space/group.  The vendor merges records by
 * id (customise), and several layers register into the same chip group (hmac
 * group 3 + hdpp group 3, hmac group 2 + wal group 2) [V], so the table count
 * must track the highest registered id + 1, not the number of records. */
static int hi5622_hcc_register_one(struct hi5622_hcc *hcc, u32 space, u32 group,
				   const struct hi5622_hcc_reg *tab, u32 count)
{
	struct hi5622_hcc_handler *handlers;
	unsigned long flags;
	u32 i, max_id = 0;

	if (!tab)
		return -EINVAL;
	handlers = hcc->handlers[space][group];

	spin_lock_irqsave(&hcc->lock, flags);
	for (i = 0; i < count; i++) {
		const struct hi5622_hcc_reg *r = &tab[i];

		if (r->id >= HI5622_HCC_GROUP_MAX)
			continue;
		handlers[r->id].id = r->id;
		handlers[r->id].fn = r->fn;
		handlers[r->id].aux = r->aux;
		if (r->id + 1 > max_id)
			max_id = r->id + 1;
	}
	if (max_id > hcc->groups[space][group].count)
		hcc->groups[space][group].count = max_id;
	spin_unlock_irqrestore(&hcc->lock, flags);
	return 0;
}

int hi5622_hcc_register_tab_chip(struct hi5622_hcc *hcc, u32 group,
				 const struct hi5622_hcc_reg *tab, u32 count)
{
	int ret;

	if (!hcc || group >= HI5622_HCC_GROUPS) {
		dev_warn(hcc->chip->dev, "hcc: bad chip table (group %u)\n",
			 group);
		return -EINVAL;
	}
	ret = hi5622_hcc_register_one(hcc, HI5622_HCC_SPACE_CHIP, group, tab,
				      count);
	if (!ret)
		dev_info(hcc->chip->dev,
			 "hcc: chip space group %u table registered (%u ids, max id %u)\n",
			 group, count, hcc->groups[HI5622_HCC_SPACE_CHIP][group].count - 1);
	return ret;
}

int hi5622_hcc_register_tab_core(struct hi5622_hcc *hcc, u32 group,
				 const struct hi5622_hcc_reg *tab, u32 count)
{
	int ret;

	if (!hcc || group >= HI5622_HCC_GROUPS)
		return -EINVAL;

	/* customise(1)+customise(2): one registration serves both per-core
	 * register spaces (hcc-re 6.3 [V]). */
	ret = hi5622_hcc_register_one(hcc, HI5622_HCC_SPACE_CORE0, group, tab,
				      count);
	if (ret)
		return ret;
	ret = hi5622_hcc_register_one(hcc, HI5622_HCC_SPACE_CORE1, group, tab,
				      count);
	if (!ret)
		dev_info(hcc->chip->dev,
			 "hcc: core spaces group %u table registered (%u ids)\n",
			 group, count);
	return ret;
}

/* ---------------------------------------------------------------------
 * Message allocation / TX
 * ------------------------------------------------------------------- */
struct hi5622_hcc_msg *hi5622_hcc_msg_alloc(u32 payload_len, gfp_t gfp)
{
	struct hi5622_hcc_msg *msg;

	if (payload_len > HI5622_HCC_MAX_PAYLOAD)
		return NULL;

	msg = kzalloc(struct_size(msg, data, HI5622_HCC_HDR_LEN + payload_len),
		      gfp);
	if (!msg)
		return NULL;
	msg->len = HI5622_HCC_HDR_LEN + payload_len;

	return msg;
}

void hi5622_hcc_msg_free(struct hi5622_hcc_msg *msg)
{
	kfree(msg);
}

void hi5622_hcc_msg_set_hdr(struct hi5622_hcc_msg *msg, u8 group, u8 rgroup,
			    u8 field_a, u8 field_b, u16 id, u8 field_c)
{
	hcc_hdr_store(msg, group, rgroup, field_a, field_b, id, field_c);
}

int hi5622_hcc_tx(struct hi5622_chip *chip, struct hi5622_hcc_msg *msg)
{
	struct hi5622_hcc *hcc = &chip->hcc;
	unsigned long flags;

	if (!msg)
		return -EINVAL;
	if (msg->len < HI5622_HCC_HDR_LEN) {
		hi5622_hcc_msg_free(msg);
		return -EINVAL;
	}

	spin_lock_irqsave(&hcc->lock, flags);
	list_add_tail(&msg->node, &hcc->txq);
	hcc->tx_queued++;
	spin_unlock_irqrestore(&hcc->lock, flags);

	queue_work(chip->wq, &hcc->tx_work);
	return 0;
}

void hi5622_hcc_kick(struct hi5622_chip *chip)
{
	queue_work(chip->wq, &chip->hcc.tx_work);
}

/* ---------------------------------------------------------------------
 * RX dispatch (hcc_msg_process mirror, hcc-re 5.2 / driver-re 3.2 [V])
 * ------------------------------------------------------------------- */
int hi5622_hcc_process_space(struct hi5622_chip *chip, u32 space,
			     const void *buf, u32 len)
{
	struct hi5622_hcc *hcc = &chip->hcc;
	struct hi5622_hcc_rx rx;
	struct hi5622_hcc_handler *h;
	const u8 *p = buf;
	unsigned long flags;
	u16 wire_len;

	if (!buf || len < HI5622_HCC_HDR_LEN)
		return -EINVAL;
	if (space >= HI5622_HCC_SPACES)
		return -EINVAL;

	memset(&rx, 0, sizeof(rx));
	rx.data = p;
	rx.len = len;
	rx.wire_len = get_unaligned_le16(p + 4);
	rx.space = (u8)space;
	rx.group = p[0] & 0x0f;
	rx.rgroup = p[1] >> 4;
	rx.id = get_unaligned_le16(p + 6);
	wire_len = rx.wire_len;

	hcc->rx_frames++;
	if (wire_len >= HI5622_HCC_HDR_LEN && wire_len != len)
		dev_dbg_ratelimited(chip->dev,
				    "hcc: frame len %u != wire len %u (space %u group %u id %u)\n",
				    len, wire_len, rx.space, rx.group, rx.id);

	if (rx.group >= HI5622_HCC_GROUPS) {
		hcc->rx_unknown++;
		dev_dbg_ratelimited(chip->dev,
				    "hcc: group %u out of range (id %u)\n",
				    rx.group, rx.id);
		return -ENOENT;
	}

	spin_lock_irqsave(&hcc->lock, flags);
	if (rx.id >= hcc->groups[space][rx.group].count) {
		spin_unlock_irqrestore(&hcc->lock, flags);
		hcc->rx_unknown++;
		dev_dbg_ratelimited(chip->dev,
				    "hcc: no handler for space %u group %u id %u (count %u)\n",
				    rx.space, rx.group, rx.id,
				    hcc->groups[space][rx.group].count);
		return -ENOENT;
	}
	h = &hcc->handlers[space][rx.group][rx.id];
	if (!h->fn) {
		spin_unlock_irqrestore(&hcc->lock, flags);
		hcc->rx_unknown++;
		dev_dbg_ratelimited(chip->dev,
				    "hcc: NULL handler for space %u group %u id %u\n",
				    rx.space, rx.group, rx.id);
		return -ENOENT;
	}
	h->stats++;
	hcc->rx_dispatched++;
	spin_unlock_irqrestore(&hcc->lock, flags);

	h->fn(chip, &rx, h->aux);
	return 0;
}

int hi5622_hcc_process(struct hi5622_chip *chip, const void *buf, u32 len)
{
	/* Control/boot messages observed so far arrive on the DR0 control
	 * channel, i.e. the chip register space (type 0) [I, hcc-re 4.1]. */
	return hi5622_hcc_process_space(chip, HI5622_HCC_SPACE_CHIP, buf, len);
}
