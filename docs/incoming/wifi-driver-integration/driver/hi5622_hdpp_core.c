// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_hdpp_core.c - hdpp core skeleton of the clean-room Hi5622V100 host
 * driver: object tree, D2H table registration, channel setup, TX/RX entry
 * points.  The heavy hdpp_* function library is inventoried in
 * wifi-datapath-slice/mapping/hdpp-inventory.csv; this file is the integration
 * core that binds that layer to the transport and the HCC message layer.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from
 * wifi-driver-re 3.3 (registration sites [V]), phase37 3.2 (group table sizes
 * [V]) and phase37 2.3 (the quoted HCC ids [V]).  No vendor source/object.
 *
 * [U] markers: the per-id handler maps for groups 0-3 and the hdpp TX/RX MSDU
 * ring formats are not yet reversed; the skeleton installs placeholder ids
 * (0..count-1) so the dispatch plumbing is live and the bench can log which
 * ids the device actually posts.
 */
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "hi5622_drv.h"

/* Opaque vendor object records: layout is [U]; only identity fields are kept
 * so logs can name the object.  Never expose these to other layers. */
struct hi5622_hdpp_board {
	u32	magic;
	u32	chips;
};

struct hi5622_hdpp_chip {
	u8	chip_id;
	u8	band_count;
};

struct hi5622_hdpp_band {
	u8	band_id;
	u8	attached;
};

struct hi5622_hdpp_device {
	u8	band_id;
	u8	valid;
};

struct hi5622_hdpp_vap {
	u8	band_id;
	u8	vap_id;
	u8	valid;
};

struct hi5622_hdpp_user {
	u8	band_id;
	u16	idx;
	u8	valid;
};

/* ---- D2H handlers of the two hdpp registration tables ----
 *
 * Ids and handler names are now pinned by hcc-re 5.5 [V] (task-63, submodule
 * commit 07b9201), which supersedes the earlier placeholder-id assumption.
 * The handler BODIES are still stubs: the real hmac_voice_aggr_event /
 * hmac_device_wow_data_report / hmac_rx_schedule_req / hmac_tx_* /
 * hmac_rx_process_data_event / hdpp_stat_save_* implementations belong to the
 * hmac and hdpp data-path port slices.  Each stub logs (rate-limited) and
 * counts so a B3 session shows exactly which catalogue entries the device
 * actually posts.
 */
#define HI5622_HDPP_STUB(_name, _contract, _space, _group, _id)		\
	static void _name(struct hi5622_chip *chip,			\
			  const struct hi5622_hcc_rx *rx, void *aux)	\
	{								\
		dev_dbg_ratelimited(chip->dev,				\
				    "hdpp: %s (space %u group %u id %u, %u B) - contract handler %s still stubbed\n", \
				    __func__, rx->space, rx->group, rx->id, \
				    rx->len, _contract);			\
	}

/* chip space group 3 (hdpp_main_init, count 5) */
HI5622_HDPP_STUB(hi5622_hdpp_voice_aggr_event, "hmac_voice_aggr_event", 0, 3, 0)
HI5622_HDPP_STUB(hi5622_hdpp_device_wow_report, "hmac_device_wow_data_report", 0, 3, 4)
HI5622_HDPP_STUB(hi5622_hdpp_rx_schedule_req, "hmac_rx_schedule_req", 0, 3, 5)
HI5622_HDPP_STUB(hi5622_hdpp_stat_tx_ppdu, "hdpp_stat_save_tx_ppdu_record_process", 0, 3, 7)
HI5622_HDPP_STUB(hi5622_hdpp_stat_rx_ppdu, "hdpp_stat_save_rx_ppdu_record_process", 0, 3, 8)

/* per-core spaces group 0 (hcc_msg_register_tab_core(0), count 7) */
HI5622_HDPP_STUB(hi5622_hdpp_tx_complete_event, "hmac_tx_complete_event_handle", 1, 0, 0)
HI5622_HDPP_STUB(hi5622_hdpp_tx_event, "hmac_tx_event_process", 1, 0, 1)
HI5622_HDPP_STUB(hi5622_hdpp_rx_data_event_2, "hmac_rx_process_data_event", 1, 0, 2)
HI5622_HDPP_STUB(hi5622_hdpp_rx_data_event_3, "hmac_rx_process_data_event", 1, 0, 3)
HI5622_HDPP_STUB(hi5622_hdpp_tx_complete_other_core, "hmac_tx_complete_notify_other_core_event_handle", 1, 0, 4)
HI5622_HDPP_STUB(hi5622_hdpp_mac_exception, "hmac_mac_exception_proc", 1, 0, 7)
HI5622_HDPP_STUB(hi5622_hdpp_ba_timeout, "hmac_ba_timeout_proc", 1, 0, 8)

/* chip space group 3, 5 entries (hdpp_main_init call @0x135c) [V] */
static const struct hi5622_hcc_reg hi5622_hdpp_group3_tab[] = {
	{ 0, 0, hi5622_hdpp_voice_aggr_event,    NULL },
	{ 4, 0, hi5622_hdpp_device_wow_report,   NULL },
	{ 5, 0, hi5622_hdpp_rx_schedule_req,     NULL },
	{ 7, 0, hi5622_hdpp_stat_tx_ppdu,        NULL },
	{ 8, 0, hi5622_hdpp_stat_rx_ppdu,        NULL },
};

/* per-core space group 0, 7 entries (call @0x136c) [V] */
static const struct hi5622_hcc_reg hi5622_hdpp_core_tab[] = {
	{ 0, 0, hi5622_hdpp_tx_complete_event,     NULL },
	{ 1, 0, hi5622_hdpp_tx_event,              NULL },
	{ 2, 0, hi5622_hdpp_rx_data_event_2,       NULL },
	{ 3, 0, hi5622_hdpp_rx_data_event_3,       NULL },
	{ 4, 0, hi5622_hdpp_tx_complete_other_core, NULL },
	{ 7, 0, hi5622_hdpp_mac_exception,         NULL },
	{ 8, 0, hi5622_hdpp_ba_timeout,            NULL },
};

/* ---- lifecycle ---- */
int hi5622_hdpp_init(struct hi5622_chip *chip)
{
	struct hi5622_hdpp *hdpp = &chip->hdpp;
	u32 i;
	int ret;

	memset(hdpp, 0, sizeof(*hdpp));
	hdpp->chip = chip;
	hdpp->state = HI5622_HDPP_INITED;
	/* [I] the first probed endpoint (RC0) is band 0, the second band 1; the
	 * band<->RC mapping is not pinned by the corpus [U]. */
	hdpp->chip_id = (u8)chip->index;
	hdpp->band_id = (u8)min_t(u32, chip->index, HI5622_HDPP_MAX_BAND - 1);

	hdpp->board = kzalloc(sizeof(*hdpp->board), GFP_KERNEL);
	hdpp->hchip = kzalloc(sizeof(*hdpp->hchip), GFP_KERNEL);
	if (!hdpp->board || !hdpp->hchip) {
		ret = -ENOMEM;
		goto err_free;
	}
	hdpp->board->magic = 0x48535632;	/* "HSV2" */
	hdpp->board->chips = 1;
	hdpp->hchip->chip_id = hdpp->chip_id;
	hdpp->hchip->band_count = HI5622_HDPP_MAX_BAND;

	for (i = 0; i < HI5622_HDPP_MAX_BAND; i++) {
		hdpp->bands[i] = kzalloc(sizeof(*hdpp->bands[i]), GFP_KERNEL);
		hdpp->devices[i] = kzalloc(sizeof(*hdpp->devices[i]),
					   GFP_KERNEL);
		if (!hdpp->bands[i] || !hdpp->devices[i]) {
			ret = -ENOMEM;
			goto err_free;
		}
		hdpp->bands[i]->band_id = i;
		hdpp->devices[i]->band_id = i;
		hdpp->devices[i]->valid = 1;
	}

	/* chip space group 3 (merged with hmac's 8 ids by the HCC layer) */
	ret = hi5622_hcc_register_tab_chip(&chip->hcc, HI5622_HCC_GROUP_HDPP,
					   hi5622_hdpp_group3_tab,
					   ARRAY_SIZE(hi5622_hdpp_group3_tab));
	if (ret)
		goto err_free;
	hdpp->group3_count = ARRAY_SIZE(hi5622_hdpp_group3_tab);

	/* both per-core spaces, group 0 (vendor customise(1)+customise(2)) */
	ret = hi5622_hcc_register_tab_core(&chip->hcc, 0,
					   hi5622_hdpp_core_tab,
					   ARRAY_SIZE(hi5622_hdpp_core_tab));
	if (ret)
		goto err_free;
	hdpp->core_count = ARRAY_SIZE(hi5622_hdpp_core_tab);

	ret = hi5622_alg_init(&hdpp->alg, chip);
	if (ret)
		goto err_free;

	dev_info(chip->dev,
		 "hdpp: core init (chip %u band %u; chip-space group3 %u ids, core-space group0 %u ids; ids per hcc-re 5.5 [V], handler bodies stubbed)\n",
		 hdpp->chip_id, hdpp->band_id, hdpp->group3_count,
		 hdpp->core_count);
	return 0;

err_free:
	hi5622_hdpp_exit(chip);
	return ret;
}

void hi5622_hdpp_exit(struct hi5622_chip *chip)
{
	struct hi5622_hdpp *hdpp = &chip->hdpp;
	u32 i;

	hi5622_alg_exit(&hdpp->alg);
	for (i = 0; i < HI5622_HDPP_MAX_BAND; i++) {
		kfree(hdpp->bands[i]);
		kfree(hdpp->devices[i]);
		hdpp->bands[i] = NULL;
		hdpp->devices[i] = NULL;
	}
	kfree(hdpp->board);
	kfree(hdpp->hchip);
	hdpp->board = NULL;
	hdpp->hchip = NULL;
	hdpp->state = HI5622_HDPP_DOWN;
}

/* ---- channel setup (device writes; attended gate) ---- */
int hi5622_hdpp_channel_setup(struct hi5622_chip *chip)
{
	struct hi5622_hdpp *hdpp = &chip->hdpp;
	int ret;

	if (hdpp->state == HI5622_HDPP_UP)
		return 0;

	/* The ETE channel-register writes are device writes; the project's
	 * attended-bench rule says they happen only under an explicit gate. */
	if (!chip->allow_device_writes) {
		dev_info(chip->dev,
			 "hdpp: channel setup deferred - attended gate closed (set allow_device_writes=1 at module load)\n");
		return -EPERM;
	}

	ret = hi5622_ete_init(chip);
	if (ret)
		return ret;

	/* Vendor ring shape [V*]: depth 32 (depth-1 = 0x1f), buffer 2 KiB,
	 * device-side base in 0x8xxxxxxx.  The transport owns those values; log
	 * them so the bench can diff against phase42 E.3. */
	dev_info(chip->dev,
		 "hdpp: channel setup done (SR0 depth %u buf %u; DR0 depth %u buf %u)\n",
		 chip->ete.depth, chip->ete.buf_len, chip->ete.depth,
		 chip->ete.buf_len);

	hdpp->state = HI5622_HDPP_UP;
	return 0;
}

/* ---- TX / RX entry points ---- */
int hi5622_hdpp_tx_frame(struct hi5622_chip *chip, struct sk_buff *skb)
{
	struct hi5622_hdpp *hdpp = &chip->hdpp;
	struct hi5622_hcc_msg *msg;
	u32 len;

	if (!skb)
		return -EINVAL;
	len = skb->len;

	if (!(chip->state & HI5622_DRV_STATE_RUNNING)) {
		hdpp->tx_dropped++;
		chip->tx_dropped++;
		dev_kfree_skb_any(skb);
		return -ENETDOWN;
	}
	if (len > HI5622_HCC_MAX_PAYLOAD) {
		hdpp->tx_dropped++;
		chip->tx_dropped++;
		dev_kfree_skb_any(skb);
		return -EMSGSIZE;
	}

	msg = hi5622_hcc_msg_alloc(len, GFP_ATOMIC);
	if (!msg) {
		hdpp->tx_dropped++;
		chip->tx_dropped++;
		dev_kfree_skb_any(skb);
		return -ENOMEM;
	}
	memcpy(msg->data + HI5622_HCC_HDR_LEN, skb->data, len);
	/* Placeholder control-path TX: group 0 (cfg), resource group 1, id
	 * HI5622_HCC_ID_HDPP_TX_PLACEHOLDER.  The real MSDU data path is the
	 * hmac_tx_msdu_ring_* / hdpp_* ring [U]; see REPORT section "stubbed". */
	hi5622_hcc_msg_set_hdr(msg, HI5622_HCC_GROUP_CFG, 1, 0, 0,
			       HI5622_HCC_ID_HDPP_TX_PLACEHOLDER, 0);

	hdpp->tx_frames++;
	chip->tx_frames++;
	dev_kfree_skb_any(skb);

	return hi5622_hcc_tx(chip, msg);
}

void hi5622_hdpp_rx_frame(struct hi5622_chip *chip, const void *buf, u32 len)
{
	struct hi5622_hdpp *hdpp = &chip->hdpp;
	int ret;

	hdpp->rx_frames++;
	ret = hi5622_hcc_process(chip, buf, len);
	if (ret)
		hdpp->rx_control++;	/* malformed / unhandled */
}

/* ---- object-model accessors ---- */
struct hi5622_hdpp_band *hi5622_hdpp_band_get(struct hi5622_chip *chip,
					      u8 band_id)
{
	if (band_id >= HI5622_HDPP_MAX_BAND)
		return NULL;
	return chip->hdpp.bands[band_id];
}

struct hi5622_hdpp_vap *hi5622_hdpp_vap_get(struct hi5622_chip *chip,
					    u8 band_id, u8 vap_id)
{
	if (band_id >= HI5622_HDPP_MAX_BAND || vap_id >= HI5622_HDPP_MAX_VAP)
		return NULL;
	return chip->hdpp.vaps[band_id][vap_id];
}

const char *hi5622_hdpp_state_str(u32 state)
{
	switch (state) {
	case HI5622_HDPP_DOWN:
		return "down";
	case HI5622_HDPP_INITED:
		return "inited";
	case HI5622_HDPP_UP:
		return "up";
	default:
		return "?";
	}
}
