// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_alg.c - alg configuration skeleton of the clean-room Hi5622V100 host
 * driver.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from
 * wifi-driver-re sections 3.7/7.1 and phase37 2.3.  No vendor source/object.
 *
 * What the vendor does (evidence):
 *   - The alg layer owns a 414-entry alg/cali configuration table whose
 *     contents are opaque binary config (wifi-firmware-re 5.9 [V*]).
 *   - The H2D config event builder is hmac_config_alg_send_event, HCC group 0
 *     id 2 (phase37 2.3 [V]).
 *   - The response is routed through hmac_event_config_syn ->
 *     hmac_sync_dmac_alg_cfg_rsp_entry (wifi-driver-re 3.6/3.7 [V]), and
 *     wifi-hcc-re 6.2 [V] pins the carrier: chip-space group 1 id 3
 *     (hmac_event_config_syn).  That table belongs to the hmac port, so this
 *     skeleton cannot register it; the future hmac port calls
 *     hi5622_alg_cfg_response() from there.  The request/response pairing word
 *     (vendor convention 0x0d01xxxx, 2g/5g variants) and the response payload
 *     layout remain [U].
 *
 * This skeleton implements the table store, the H2D request build (chip group
 * 0 id 2) and the completion wait, but keeps the protocol DISABLED by default:
 * the hmac_event_config_syn dispatcher that lands the response is not
 * implemented yet, so a sync request would block until timeout.  Set the drv
 * module parameter alg_protocol=1 only after the hmac port calls
 * hi5622_alg_cfg_response() from its chip-group-1 id-3 handler.
 */
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "hi5622_drv.h"

int hi5622_alg_init(struct hi5622_alg *alg, struct hi5622_chip *chip)
{
	memset(alg, 0, sizeof(*alg));
	alg->chip = chip;
	spin_lock_init(&alg->lock);
	dev_info(chip->dev,
		 "alg: cfg skeleton init (414-entry vendor table [V*]; %u descriptors max, sync protocol [U] disabled)\n",
		 HI5622_ALG_CFG_MAX);
	return 0;
}

void hi5622_alg_exit(struct hi5622_alg *alg)
{
	unsigned int i;

	for (i = 0; i < HI5622_ALG_CFG_MAX; i++) {
		kfree(alg->ents[i].data);
		alg->ents[i].data = NULL;
		alg->ents[i].len = 0;
	}
	alg->count = 0;
}

static struct hi5622_alg_ent *hi5622_alg_find(struct hi5622_alg *alg, u16 id)
{
	unsigned int i;

	for (i = 0; i < alg->count; i++)
		if (alg->ents[i].id == id)
			return &alg->ents[i];
	return NULL;
}

int hi5622_alg_cfg_store(struct hi5622_chip *chip, u16 id, const void *data,
			 u32 len)
{
	struct hi5622_alg *alg = &chip->hdpp.alg;
	struct hi5622_alg_ent *ent;
	unsigned long flags;
	void *copy = NULL;

	if (!data || !len || len > 0xffff)
		return -EINVAL;

	copy = kmemdup(data, len, GFP_KERNEL);
	if (!copy)
		return -ENOMEM;

	spin_lock_irqsave(&alg->lock, flags);
	ent = hi5622_alg_find(alg, id);
	if (ent) {
		kfree(ent->data);
		ent->data = copy;
		ent->len = (u16)len;
		spin_unlock_irqrestore(&alg->lock, flags);
		return 0;
	}
	if (alg->count >= HI5622_ALG_CFG_MAX) {
		spin_unlock_irqrestore(&alg->lock, flags);
		kfree(copy);
		return -ENOSPC;
	}
	ent = &alg->ents[alg->count++];
	ent->id = id;
	ent->len = (u16)len;
	ent->data = copy;
	spin_unlock_irqrestore(&alg->lock, flags);
	return 0;
}

/**
 * hi5622_alg_cfg_sync() - send one alg config event and wait for its response
 *
 * Builds the H2D hmac_config_alg_send_event (HCC group 0 id 2 [V]) with a
 * skeleton-defined payload {le16 seq, le16 cfg_id, le16 len, data...}.  The
 * real payload layout and the response id are [U]; therefore the protocol is
 * disabled unless the caller opened alg->enabled (drv module parameter
 * alg_protocol=1, intended for the first B2 bench session).
 */
int hi5622_alg_cfg_sync(struct hi5622_chip *chip, u16 cfg_id,
			const void *data, u32 len, u32 timeout_ms)
{
	struct hi5622_alg *alg = &chip->hdpp.alg;
	struct hi5622_hcc_msg *msg;
	DECLARE_COMPLETION_ONSTACK(rsp);
	unsigned long flags;
	u32 payload = 6 + len;
	u16 seq;
	long waited;
	int ret;

	if (!alg->enabled) {
		dev_info_ratelimited(chip->dev,
				     "alg: cfg sync not wired yet (response HCC id [U]); refusing request id 0x%04x\n",
				     cfg_id);
		return -EOPNOTSUPP;
	}
	if (payload > HI5622_HCC_MAX_PAYLOAD)
		return -EMSGSIZE;

	msg = hi5622_hcc_msg_alloc(payload, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	spin_lock_irqsave(&alg->lock, flags);
	seq = (u16)++alg->sync_seq;
	alg->rsp = &rsp;
	alg->rsp_id = cfg_id;
	spin_unlock_irqrestore(&alg->lock, flags);

	msg->data[HI5622_HCC_HDR_LEN + 0] = (u8)seq;
	msg->data[HI5622_HCC_HDR_LEN + 1] = (u8)(seq >> 8);
	msg->data[HI5622_HCC_HDR_LEN + 2] = (u8)cfg_id;
	msg->data[HI5622_HCC_HDR_LEN + 3] = (u8)(cfg_id >> 8);
	msg->data[HI5622_HCC_HDR_LEN + 4] = (u8)len;
	msg->data[HI5622_HCC_HDR_LEN + 5] = (u8)(len >> 8);
	if (len)
		memcpy(msg->data + HI5622_HCC_HDR_LEN + 6, data, len);

	hi5622_hcc_msg_set_hdr(msg, HI5622_HCC_GROUP_CFG, 1, 0, 0,
			       HI5622_HCC_ID_CFG_ALG_EVENT, 0);

	alg->sync_requests++;
	ret = hi5622_hcc_tx(chip, msg);
	if (ret) {
		spin_lock_irqsave(&alg->lock, flags);
		alg->rsp = NULL;
		spin_unlock_irqrestore(&alg->lock, flags);
		return ret;
	}

	waited = wait_for_completion_timeout(&rsp, msecs_to_jiffies(timeout_ms));
	spin_lock_irqsave(&alg->lock, flags);
	alg->rsp = NULL;
	spin_unlock_irqrestore(&alg->lock, flags);
	if (!waited) {
		alg->sync_timeouts++;
		dev_warn(chip->dev,
			 "alg: cfg sync id 0x%04x timed out after %u ms (response id [U])\n",
			 cfg_id, timeout_ms);
		return -ETIMEDOUT;
	}
	return 0;
}

/* Response entry point for the future hmac_event_config_syn handler.  The D2H
 * HCC id that carries it is [U]; when it is pinned, the owning handler calls
 * this with the request id from the response payload. */
void hi5622_alg_cfg_response(struct hi5622_chip *chip, u16 cfg_id)
{
	struct hi5622_alg *alg = &chip->hdpp.alg;
	unsigned long flags;
	struct completion *rsp = NULL;

	spin_lock_irqsave(&alg->lock, flags);
	if (alg->rsp && alg->rsp_id == cfg_id)
		rsp = alg->rsp;
	alg->sync_responses++;
	spin_unlock_irqrestore(&alg->lock, flags);

	if (rsp)
		complete(rsp);
	else
		dev_dbg_ratelimited(chip->dev,
				    "alg: unexpected cfg response id 0x%04x\n",
				    cfg_id);
}

void hi5622_alg_cfg_dump(const struct hi5622_alg *alg, char *buf, size_t len)
{
	unsigned int i;
	size_t pos = 0;

	pos += scnprintf(buf + pos, len - pos,
			 "alg: entries=%u sync_req=%u rsp=%u timeout=%u enabled=%d\n",
			 alg->count, alg->sync_requests, alg->sync_responses,
			 alg->sync_timeouts, alg->enabled);
	for (i = 0; i < alg->count && pos < len; i++)
		pos += scnprintf(buf + pos, len - pos,
				 "  cfg 0x%04x len %u\n",
				 alg->ents[i].id, alg->ents[i].len);
}
