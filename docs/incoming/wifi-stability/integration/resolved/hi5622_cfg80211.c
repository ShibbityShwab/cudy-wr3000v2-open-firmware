// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_cfg80211.c - cfg80211 full-MAC wiphy layer (M0-M2) for the
 * Hi5622V100 clean-room Wi-Fi driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-cfg80211 lane, shared task task-104,
 * 2026-11-24).  No vendor source/object/firmware/calibration material is
 * reproduced; the report ../REPORT.md carries the per-claim evidence markers.
 *
 * Responsibilities:
 *   - allocate one wiphy per PCI endpoint (one radio per endpoint; the two
 *     endpoints are 0000:00:00.0 / 0001:00:00.0, DRIVER-BLACKBOX sec 1);
 *   - fill the band/channel/rate/cipher capabilities derivable from the RE
 *     (2.4 GHz ch 1..13; 5 GHz LU regdomain plan 36..48, 52..64,
 *     100..140; max power 30 dBm 2.4 / 23 dBm 5; standard cipher suites);
 *   - register the wiphy ONLY after the HCC ready handshake completed
 *     (g4 id1 + g4 id2), matching the vendor order (driver init after ready,
 *     wifi-vendor-userspace sec 2); the registration runs in a work item so
 *     the service thread is never blocked;
 *   - teardown: delete every VAP netdev under RTNL, then unregister + free
 *     the wiphy (the service thread is stopped before this runs).
 *
 * SAFETY: this file performs no device access and sends no H2D message.  The
 * first H2D message remains the h2d-send lane's TXQ config sync / context
 * create path, gated by the portable hcc_core state machine.
 */
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_driver.h"
#include "hi5622_hcc.h"
#include "hi5622_cfg80211.h"
#include "hi5622_keys.h"
#include "hi5622_sta.h"
#include "hi5622_sta_logic.h"

/* ---------------------------------------------------------------------
 * Module parameters (host-only bench experiments; defaults are the safe
 * production behaviour).
 * -------------------------------------------------------------------*/
unsigned int hi5622_cfg11_auto = 1;
module_param_named(cfg80211_auto, hi5622_cfg11_auto, uint, 0644);
MODULE_PARM_DESC(cfg80211_auto,
		 "register the wiphy automatically once the HCC ready handshake is decoded (default 1)");

int hi5622_band_force = -1;
module_param_named(band_force, hi5622_band_force, int, 0644);
MODULE_PARM_DESC(band_force,
		 "-1 = auto per PCI domain (domain 0 = 2.4 GHz [I], domain 1 = 5 GHz); 0 = force 2.4 GHz; 1 = force 5 GHz");

/* wifi-sta-static lane (task-141): capability/table parameters.  Defaults
 * are the bench-validated baseline; the vendor-derived words/tables are
 * opt-in and their evidence is in docs/incoming/wifi-sta-static/REPORT.md
 * (static band tables .data+0xdf8 / .data+0xe54 of the 2.5.24 module). */
unsigned int hi5622_ht_mode = HI5622_STA_HT_MODE_BASELINE;
module_param_named(ht_mode, hi5622_ht_mode, uint, 0644);
MODULE_PARM_DESC(ht_mode,
		 "HT capability: 0=baseline HT20 (default), 1=vendor HT20 word (SGI20), 2=vendor HT20/40 word 0x62 (SUP_WIDTH_20_40|SGI20|SGI40)");

unsigned int hi5622_vht_cap;
module_param_named(vht_cap, hi5622_vht_cap, uint, 0644);
MODULE_PARM_DESC(vht_cap,
		 "advertise the vendor VHT capability (2 SS MCS0-9; 5 GHz: 160 MHz/SGI80/SGI160; 2.4 GHz: HTC_VHT only; 0=off default)");

unsigned int hi5622_band_vendor;
module_param_named(band_vendor, hi5622_band_vendor, uint, 0644);
MODULE_PARM_DESC(band_vendor,
		 "use the vendor band tables instead of the shipped lists (2.4 GHz ch1-14, 5 GHz 29 channels incl. ch144 and the 4.9 GHz block; 0=off default)");

/* Documentation-purpose locally-administered address (02:...) - never a
 * per-unit MAC; each VAP derives its own last byte from the slot. */
static const u8 hi5622_cfg11_perm_addr[ETH_ALEN] = {
	0x02, 0x00, 0x68, 0x69, 0x35, 0x00,
};

/* Standard cipher suites a full-MAC AP/STA must advertise for stock
 * hostapd/wpa_supplicant; the vendor key path itself is M3 (gap G1). */
static const u32 hi5622_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_GCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
	WLAN_CIPHER_SUITE_BIP_CMAC_256,
};

/* ---------------------------------------------------------------------
 * Band tables
 *
 * 2.4 GHz: channels 1..13 (2412..2472 MHz) - the vendor's live wiphy advertises
 * 30.0 dBm on 2.4 GHz (wifi-driver-re sec 2.3 [V*]); channels 12/13 are left
 * for cfg80211's regulatory core to disable per regdomain (phase14 evidence).
 *
 * 5 GHz: the shipped INI LU custom-regdomain plan (phase2/power-decode.md
 * sec (b)): BAND1 5170-5250 -> ch 36/40/44/48, BAND2 5250-5330 -> ch
 * 52/56/60/64 (DFS+indoor), BAND3 5490-5710 -> ch 100..140 (DFS+indoor).
 * The vendor's live 5 GHz readout is 23.0/24.0 dBm, hence max_power 23 here.
 * Channel 144 and 149-196 are outside the LU plan and NOT advertised.
 * 160 MHz operation on ch 44 is observed live but the VHT/HE capability
 * tables come from the INI in M3+; M0-M2 advertises an HT20 skeleton.
 * -------------------------------------------------------------------*/
#define HI5622_CH2G(ch, freq)						\
	{ .band = NL80211_BAND_2GHZ, .center_freq = (freq),		\
	  .hw_value = (ch), .max_power = 30, .flags = 0 }
#define HI5622_CH5G(ch, freq, fl)					\
	{ .band = NL80211_BAND_5GHZ, .center_freq = (freq),		\
	  .hw_value = (ch), .max_power = 23, .flags = (fl) }
#define HI5622_DFS	(IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_RADAR |	\
			 IEEE80211_CHAN_INDOOR_ONLY)

static const struct ieee80211_channel hi5622_channels_2g[] = {
	HI5622_CH2G(1, 2412), HI5622_CH2G(2, 2417), HI5622_CH2G(3, 2422),
	HI5622_CH2G(4, 2427), HI5622_CH2G(5, 2432), HI5622_CH2G(6, 2437),
	HI5622_CH2G(7, 2442), HI5622_CH2G(8, 2447), HI5622_CH2G(9, 2452),
	HI5622_CH2G(10, 2457), HI5622_CH2G(11, 2462), HI5622_CH2G(12, 2467),
	HI5622_CH2G(13, 2472),
};

static const struct ieee80211_rate hi5622_rates_2g[] = {
	{ .bitrate = 10,  .hw_value = 0 },
	{ .bitrate = 20,  .hw_value = 1 },
	{ .bitrate = 55,  .hw_value = 2 },
	{ .bitrate = 110, .hw_value = 3 },
	{ .bitrate = 60,  .hw_value = 4 },
	{ .bitrate = 90,  .hw_value = 5 },
	{ .bitrate = 120, .hw_value = 6 },
	{ .bitrate = 180, .hw_value = 7 },
	{ .bitrate = 240, .hw_value = 8 },
	{ .bitrate = 360, .hw_value = 9 },
	{ .bitrate = 480, .hw_value = 10 },
	{ .bitrate = 540, .hw_value = 11 },
};

static const struct ieee80211_channel hi5622_channels_5g[] = {
	HI5622_CH5G(36, 5180, 0),   HI5622_CH5G(40, 5200, 0),
	HI5622_CH5G(44, 5220, 0),   HI5622_CH5G(48, 5240, 0),
	HI5622_CH5G(52, 5260, HI5622_DFS),
	HI5622_CH5G(56, 5280, HI5622_DFS),
	HI5622_CH5G(60, 5300, HI5622_DFS),
	HI5622_CH5G(64, 5320, HI5622_DFS),
	HI5622_CH5G(100, 5500, HI5622_DFS),
	HI5622_CH5G(104, 5520, HI5622_DFS),
	HI5622_CH5G(108, 5540, HI5622_DFS),
	HI5622_CH5G(112, 5560, HI5622_DFS),
	HI5622_CH5G(116, 5580, HI5622_DFS),
	HI5622_CH5G(120, 5600, HI5622_DFS),
	HI5622_CH5G(124, 5620, HI5622_DFS),
	HI5622_CH5G(128, 5640, HI5622_DFS),
	HI5622_CH5G(132, 5660, HI5622_DFS),
	HI5622_CH5G(136, 5680, HI5622_DFS),
	HI5622_CH5G(140, 5700, HI5622_DFS),
};

static const struct ieee80211_rate hi5622_rates_5g[] = {
	{ .bitrate = 60,  .hw_value = 0 },
	{ .bitrate = 90,  .hw_value = 1 },
	{ .bitrate = 120, .hw_value = 2 },
	{ .bitrate = 180, .hw_value = 3 },
	{ .bitrate = 240, .hw_value = 4 },
	{ .bitrate = 360, .hw_value = 5 },
	{ .bitrate = 480, .hw_value = 6 },
	{ .bitrate = 540, .hw_value = 7 },
};

/* HT20-only capability skeleton (the vendor's HT/VHT/HE caps come from the
 * INI tables in M3+; SGI20 + AMSDU are the two RE-visible baseline bits). */
#define HI5622_HT_CAP20							\
	{ .ht_supported = true,						\
	  .cap = IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_MAX_AMSDU,	\
	  .ampdu_factor = 3,						\
	  .ampdu_density = 0,						\
	  .mcs = { .rx_mask = { 0xff, 0, 0, 0 },			\
		   .rx_highest = 0,					\
		   .tx_params = 0,					\
		   .reserved = { 0, 0, 0 } } }

static const struct ieee80211_supported_band hi5622_band_2g_tpl = {
	.band = NL80211_BAND_2GHZ,
	.channels = (struct ieee80211_channel *)hi5622_channels_2g,
	.n_channels = ARRAY_SIZE(hi5622_channels_2g),
	.bitrates = (struct ieee80211_rate *)hi5622_rates_2g,
	.n_bitrates = ARRAY_SIZE(hi5622_rates_2g),
	.ht_cap = HI5622_HT_CAP20,
};

static const struct ieee80211_supported_band hi5622_band_5g_tpl = {
	.band = NL80211_BAND_5GHZ,
	.channels = (struct ieee80211_channel *)hi5622_channels_5g,
	.n_channels = ARRAY_SIZE(hi5622_channels_5g),
	.bitrates = (struct ieee80211_rate *)hi5622_rates_5g,
	.n_bitrates = ARRAY_SIZE(hi5622_rates_5g),
	.ht_cap = HI5622_HT_CAP20,
};

/* ---------------------------------------------------------------------
 * Band choice: one radio per endpoint.  domain 0 = 2.4 GHz, domain 1 =
 * 5 GHz is the phase15 reading [I] - the vendor uses is_pcie_cross=1, so
 * this is NOT proven (wifi-thermal-re sec 2.1); band_force overrides it.
 * -------------------------------------------------------------------*/
static int hi5622_cfg11_pick_band(struct hi5622_pci *ctx)
{
	int domain;

	if (hi5622_band_force == HI5622_BAND_2GHZ ||
	    hi5622_band_force == HI5622_BAND_5GHZ)
		return hi5622_band_force;
	if (hi5622_band_force != -1) {
		dev_warn(&ctx->pdev->dev,
			 "band_force=%d invalid (use -1 auto, 0=2.4G, 1=5G); using auto\n",
			 hi5622_band_force);
	}
	domain = pci_domain_nr(ctx->pdev->bus);
	return domain == 1 ? HI5622_BAND_5GHZ : HI5622_BAND_2GHZ;
}

static const char *hi5622_cfg11_band_name(u8 band)
{
	return band == HI5622_BAND_2GHZ ? "2.4GHz" : "5GHz";
}

/* Vendor-derived channel array (hi5622_sta_logic.c tables: 2.4 GHz ch1-14,
 * 5 GHz 29 channels including ch144 and the 4.9 GHz public-safety block).
 * Only used when the band_vendor module parameter is set; the shipped
 * default keeps the bench-validated 13/19-channel lists. */
static int hi5622_cfg11_vendor_channels(u8 band,
					struct ieee80211_channel **out,
					unsigned int *out_n)
{
	const struct hi5622_sta_chan *tbl;
	struct ieee80211_channel *ch;
	u16 cnt = 0, i;

	if (!out || !out_n)
		return -EINVAL;
	tbl = hi5622_sta_band_channels(band, &cnt);
	if (!tbl || !cnt)
		return -EINVAL;
	ch = kcalloc(cnt, sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;
	for (i = 0; i < cnt; i++) {
		ch[i].band = band == HI5622_BAND_2GHZ ? NL80211_BAND_2GHZ :
							NL80211_BAND_5GHZ;
		ch[i].center_freq = tbl[i].freq;
		ch[i].hw_value = tbl[i].num;
		ch[i].max_power = band == HI5622_BAND_2GHZ ? 30 : 23;
		if (tbl[i].flags & HI5622_STA_CHAN_NO_IR)
			ch[i].flags |= IEEE80211_CHAN_NO_IR;
		if (tbl[i].flags & HI5622_STA_CHAN_RADAR) {
			ch[i].flags |= IEEE80211_CHAN_RADAR;
			ch[i].flags |= IEEE80211_CHAN_NO_IR;
		}
	}
	*out = ch;
	*out_n = cnt;
	return 0;
}

/* Copy the static capability templates into the wiphy (cfg80211's
 * regulatory core modifies the channel arrays, so two wiphys must never
 * share them). */
static int hi5622_cfg11_setup_band(struct hi5622_cfg80211 *c)
{
	const struct ieee80211_supported_band *tpl;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chans;
	struct ieee80211_rate *rates;
	enum nl80211_band band_idx;
	int ret;

	if (c->band == HI5622_BAND_2GHZ) {
		tpl = &hi5622_band_2g_tpl;
		band_idx = NL80211_BAND_2GHZ;
	} else {
		tpl = &hi5622_band_5g_tpl;
		band_idx = NL80211_BAND_5GHZ;
	}
	chans = kmemdup(tpl->channels,
			tpl->n_channels * sizeof(*chans), GFP_KERNEL);
	rates = kmemdup(tpl->bitrates,
			tpl->n_bitrates * sizeof(*rates), GFP_KERNEL);
	sband = kmemdup(tpl, sizeof(*sband), GFP_KERNEL);
	if (!chans || !rates || !sband) {
		kfree(chans);
		kfree(rates);
		kfree(sband);
		return -ENOMEM;
	}
	sband->channels = chans;
	sband->bitrates = rates;

	/* wifi-sta-static (task-141): vendor HT/VHT words and band tables,
	 * opt-in through the module parameters above. */
	if (hi5622_ht_mode != HI5622_STA_HT_MODE_BASELINE) {
		sband->ht_cap.cap = hi5622_sta_ht_cap_word(hi5622_ht_mode);
		sband->ht_cap.ampdu_factor = HI5622_STA_HT_AMPDU_FACTOR;
		sband->ht_cap.ampdu_density = HI5622_STA_HT_AMPDU_DENSITY;
	}
	if (hi5622_vht_cap && band_idx == NL80211_BAND_5GHZ) {
		sband->vht_cap.vht_supported = true;
		sband->vht_cap.cap = hi5622_sta_vht_cap_word(c->band);
		sband->vht_cap.vht_mcs.rx_highest = 0;
		sband->vht_cap.vht_mcs.tx_highest = 0;
		sband->vht_cap.vht_mcs.rx_mcs_map = HI5622_STA_VHT_MCS_MAP;
		sband->vht_cap.vht_mcs.tx_mcs_map = HI5622_STA_VHT_MCS_MAP;
	}
	if (hi5622_band_vendor) {
		struct ieee80211_channel *vchans = NULL;
		unsigned int vn = 0;

		ret = hi5622_cfg11_vendor_channels(c->band, &vchans, &vn);
		if (ret) {
			kfree(sband->channels);
			kfree(sband->bitrates);
			kfree(sband);
			return ret;
		}
		kfree(sband->channels);
		sband->channels = vchans;
		sband->n_channels = (int)vn;
	}
	c->wiphy->bands[band_idx] = sband;
	return 0;
}

/* ---------------------------------------------------------------------
 * wiphy registration (post-ready, from a work item)
 * -------------------------------------------------------------------*/
static void hi5622_cfg11_reg_work(struct work_struct *work)
{
	struct hi5622_cfg80211 *c =
		container_of(work, struct hi5622_cfg80211, reg_work);
	struct device *dev = &c->ctx->pdev->dev;
	bool allowed = true;
	int ret;

	mutex_lock(&c->lock);
	if (c->removing || !c->model.wiphy_allocated || c->model.wiphy_registered)
		allowed = false;
	else
		c->model.register_attempts++;
	mutex_unlock(&c->lock);
	if (!allowed) {
		mutex_lock(&c->lock);
		c->reg_scheduled = false;
		mutex_unlock(&c->lock);
		return;
	}

	ret = wiphy_register(c->wiphy);

	mutex_lock(&c->lock);
	c->reg_scheduled = false;
	if (ret) {
		c->model.register_failures++;
		snprintf(c->model.last_error, sizeof(c->model.last_error),
			 "wiphy_register failed: %d", ret);
		mutex_unlock(&c->lock);
		dev_err(dev,
			"cfg80211: wiphy_register() failed (%d) - wiphy stays unregistered (no userspace interface); boot/service unaffected\n",
			ret);
		return;
	}
	c->model.wiphy_registered = true;
	c->wiphy_registered = true;
	mutex_unlock(&c->lock);
	dev_info(dev,
		 "cfg80211: wiphy registered after the ready handshake (band %s, ch %u/%u GHz, AP+STA+monitor, ciphers %u) - M0 gate met: iw phy / iw dev enumerate\n",
		 hi5622_cfg11_band_name(c->band),
		 c->band == HI5622_BAND_2GHZ ? 1u : 36u,
		 c->band == HI5622_BAND_2GHZ ? 13u : 140u,
		 (unsigned)ARRAY_SIZE(hi5622_cipher_suites));
}

void hi5622_cfg80211_ready(struct hi5622_pci *ctx)
{
	struct hi5622_cfg80211 *c;
	struct hcc_core *core;
	enum hi5622_reg_action act;
	bool schedule = false;

	if (!ctx || !ctx->cfg || !ctx->hcc || !hi5622_cfg11_auto)
		return;
	c = ctx->cfg;
	core = &ctx->hcc->hcc.core;

	mutex_lock(&c->lock);
	if (c->removing || c->reg_scheduled || c->model.wiphy_registered) {
		mutex_unlock(&c->lock);
		return;
	}
	act = hi5622_cfg_reg_gate(&c->model, core->boot_latched,
				  (int)core->phase, core->plat_ready,
				  core->dev_ready);
	if (act == HI5622_REG_NOW) {
		c->reg_scheduled = true;
		schedule = true;
		if (!c->ready_logged) {
			c->ready_logged = true;
			dev_info(&ctx->pdev->dev,
				 "cfg80211: ready handshake decoded (phase=READY_TXQ_SYNC, id1+id2) - scheduling wiphy registration (one-shot)\n");
		}
	}
	mutex_unlock(&c->lock);
	if (schedule)
		schedule_work(&c->reg_work);
}

/* ---------------------------------------------------------------------
 * Probe/remove lifecycle
 * -------------------------------------------------------------------*/
/* Per-iftype management frame subscription/transmit bitmaps (802.11
 * management subtype numbers; cfg80211_mlme_register_mgmt checks rx,
 * cfg80211_mlme_mgmt_tx checks tx).  The actual management TX path is
 * fail-closed (data-plane/M4 work); these bits only let cfg80211 accept the
 * hostapd frame subscriptions. */
static const struct ieee80211_txrx_stypes
hi5622_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_STATION] = {
		.tx = BIT(0x0b) | BIT(0x0d),	/* auth, action */
		.rx = BIT(0x01) | BIT(0x03) | BIT(0x05) | BIT(0x0a) |
		      BIT(0x0c) | BIT(0x0d),	/* (re)assoc resp, probe resp,
						 * disassoc/deauth, action */
	},
	[NL80211_IFTYPE_AP] = {
		.tx = BIT(0x00) | BIT(0x02) | BIT(0x04) | BIT(0x0a) |
		      BIT(0x0b) | BIT(0x0c) | BIT(0x0d),
		.rx = BIT(0x00) | BIT(0x02) | BIT(0x04) | BIT(0x0a) |
		      BIT(0x0b) | BIT(0x0c) | BIT(0x0d),
	},
	[NL80211_IFTYPE_MONITOR] = {
		.tx = 0xffff,
		.rx = 0xffff,
	},
};

int hi5622_cfg80211_init(struct hi5622_pci *ctx)
{
	struct hi5622_cfg80211 *c;
	struct wiphy *wiphy;
	int band, ret;

	if (!ctx)
		return -EINVAL;
	if (ctx->cfg) {
		dev_warn(&ctx->pdev->dev, "cfg80211: already initialised\n");
		return 0;
	}
	if (!hi5622_cfg11_auto) {
		dev_info(&ctx->pdev->dev,
			 "cfg80211: cfg80211_auto=0 - wiphy will not be registered\n");
		return 0;
	}
	if (hi5622_ht_mode > HI5622_STA_HT_MODE_MAX) {
		dev_warn(&ctx->pdev->dev,
			 "cfg80211: ht_mode=%u invalid (0..%u); using the baseline HT20 word" "\n",
			 hi5622_ht_mode, HI5622_STA_HT_MODE_MAX);
		hi5622_ht_mode = HI5622_STA_HT_MODE_BASELINE;
	}
	band = hi5622_cfg11_pick_band(ctx);
	wiphy = wiphy_new_nm(&hi5622_cfg11_ops, sizeof(*c), NULL);
	if (!wiphy) {
		dev_err(&ctx->pdev->dev, "cfg80211: wiphy_new_nm failed\n");
		return -ENOMEM;
	}
	c = wiphy_priv(wiphy);
	memset(c, 0, sizeof(*c));
	c->ctx = ctx;
	c->wiphy = wiphy;
	c->band = (u8)band;
	mutex_init(&c->lock);
	INIT_WORK(&c->reg_work, hi5622_cfg11_reg_work);
	hi5622_cfg_model_init(&c->model, (u8)band);
	c->model.wiphy_allocated = true;
	set_wiphy_dev(wiphy, &ctx->pdev->dev);

	/* capabilities */
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_MONITOR);
	wiphy->max_scan_ssids = 12;	/* vendor live wiphy value [V*] */
	memcpy(wiphy->perm_addr, hi5622_cfg11_perm_addr, ETH_ALEN);
	wiphy->cipher_suites = hi5622_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(hi5622_cipher_suites);
	/* wifi-ap-start lane (task-121, hostapd bring-up): cfg80211 refuses
	 * NL80211_CMD_REGISTER_FRAME with -EOPNOTSUPP when wiphy->mgmt_stypes is
	 * NULL (cfg80211_mlme_register_mgmt) and with -EINVAL when the per-iftype
	 * rx bitmap lacks the subtype.  hostapd's AP-mode setup subscribes to
	 * auth/assoc/reassoc/disassoc/deauth/probe-request/action frames during
	 * set_mode(AP); without this table hostapd reports "Could not configure
	 * driver mode" after a successful SET_INTERFACE.  rx/tx bitmap bit N =
	 * 802.11 management subtype N. */
	/* hostapd's first NEW_BEACON goes through nl80211_get_wiphy_data_ap()
	 * (userspace AP SME), which registers NL80211_CMD_REGISTER_BEACONS;
	 * cfg80211 rejects that registration with -EOPNOTSUPP unless the wiphy
	 * advertises WIPHY_FLAG_REPORTS_OBSS.  Without the flag hostapd returns
	 * -ENOBUFS *before* sending NEW_BEACON ("Failed to set beacon
	 * parameters", no start_ap reached).  We have no OBSS reporting
	 * implementation, so the flag only satisfies the registration
	 * bookkeeping. */
	wiphy->flags |= WIPHY_FLAG_REPORTS_OBSS;
	dev_info(&ctx->pdev->dev,
		 "cfg80211: REPORTS_OBSS flag set (hostapd wiphy-data AP registration)\n");
	wiphy->mgmt_stypes = hi5622_mgmt_stypes;
	dev_info(&ctx->pdev->dev,
		 "cfg80211: mgmt_stypes advertised (AP rx=0x%04x tx=0x%04x) - hostapd frame subscriptions accepted\n",
		 (unsigned int)hi5622_mgmt_stypes[NL80211_IFTYPE_AP].rx,
		 (unsigned int)hi5622_mgmt_stypes[NL80211_IFTYPE_AP].tx);
	if (strscpy(wiphy->fw_version, "hi5622-cleanroom",
		    sizeof(wiphy->fw_version)) < 0)
		dev_warn(&ctx->pdev->dev, "cfg80211: fw_version truncated\n");

	ret = hi5622_cfg11_setup_band(c);
	if (ret) {
		dev_err(&ctx->pdev->dev,
			"cfg80211: band setup failed (%d)\n", ret);
		wiphy_free(wiphy);
		return ret;
	}
	ret = hi5622_scan_init(c);
	if (ret) {
		dev_err(&ctx->pdev->dev,
			"cfg80211: scan layer init failed (%d)\n", ret);
		wiphy_free(wiphy);
		return ret;
	}
	ctx->cfg = c;
	dev_info(&ctx->pdev->dev,
		 "cfg80211: wiphy allocated for endpoint %s (band %s, %s; wiphy registration is gated on the HCC ready handshake; scan/AP wiring M2/M3)\n",
		 pci_name(ctx->pdev), hi5622_cfg11_band_name(c->band),
		 band == HI5622_BAND_2GHZ ? "13 channels 1-13" :
		 "19 LU-plan channels 36-48/52-64/100-140");
	return 0;
}

void hi5622_cfg80211_remove(struct hi5622_pci *ctx)
{
	struct hi5622_cfg80211 *c;
	unsigned int i, created;
	bool registered;

	if (!ctx || !ctx->cfg)
		return;
	c = ctx->cfg;
	ctx->cfg = NULL;

	mutex_lock(&c->lock);
	c->removing = true;
	mutex_unlock(&c->lock);
	cancel_work_sync(&c->reg_work);
	/* Finish any in-flight scan before the wiphy/VAP teardown: cfg80211
	 * must get exactly one scan_done for the request we own. */
	hi5622_scan_remove(c);
	/* Release the STA join host buffer, if any (wifi-sta-static). */
	hi5622_sta_remove(c);

	registered = c->wiphy_registered;
	created = c->model.netdev_created;
	if (registered) {
		/* Delete every VAP netdev under RTNL; plain unregister_netdevice()
		 * drives cfg80211's NETDEV_UNREGISTER notifier, which releases the
		 * wdev.  The netdevs are freed by netdev_run_todo at rtnl_unlock
		 * (needs_free_netdev), so the wdevs are freed after that. */
		rtnl_lock();
		for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
			if (!c->vap_dev[i])
				continue;
			unregister_netdevice(c->vap_dev[i]);
			c->vap_dev[i] = NULL;
			c->vap_wdev[i] = NULL;
		}
		rtnl_unlock();
		wiphy_unregister(c->wiphy);
	}
	if (c->wiphy->bands[NL80211_BAND_2GHZ]) {
		struct ieee80211_supported_band *s = c->wiphy->bands[NL80211_BAND_2GHZ];

		kfree(s->channels);
		kfree(s->bitrates);
		kfree(s);
		c->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	}
	if (c->wiphy->bands[NL80211_BAND_5GHZ]) {
		struct ieee80211_supported_band *s = c->wiphy->bands[NL80211_BAND_5GHZ];

		kfree(s->channels);
		kfree(s->bitrates);
		kfree(s);
		c->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	}
	wiphy_free(c->wiphy);
	dev_info(&ctx->pdev->dev,
		 "cfg80211: layer removed (registered=%d, vaps created=%u)\n",
		 registered ? 1 : 0, created);
}

/* ---------------------------------------------------------------------
 * sysfs: read-only status
 * -------------------------------------------------------------------*/
static ssize_t cfg80211_status_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hi5622_cfg80211 *c;
	char *tmp;
	int n, ret;

	(void)attr;
	if (!ctx || !ctx->cfg)
		return sysfs_emit(buf, "cfg80211 layer not initialised (auto=0 or probe pending)\n");
	c = ctx->cfg;
	tmp = kmalloc(4096, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	mutex_lock(&c->lock);
	n = hi5622_cfg_status_line(&c->model, tmp, 4096);
	mutex_unlock(&c->lock);
	if (n < 0)
		n = 0;
	if (n < 4096) {
		int m = hi5622_scan_status(c, tmp + n, 4096 - (size_t)n);

		if (m > 0)
			n += m;
	}
	if (n < 4096) {
		int m = hi5622_keys_status(c, tmp + n, 4096 - (size_t)n);

		if (m > 0)
			n += m;
	}
	ret = sysfs_emit(buf,
			 "wifi-cfg80211 M0-M3 (scan/AP wiring): auto=%u band_force=%d band=%s\n%s",
			 hi5622_cfg11_auto, hi5622_band_force,
			 hi5622_cfg11_band_name(c->band), tmp);
	kfree(tmp);
	return ret;
}
static DEVICE_ATTR_RO(cfg80211_status);

static struct attribute *hi5622_cfg11_attrs[] = {
	&dev_attr_cfg80211_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hi5622_cfg11);

int hi5622_cfg80211_add_attrs(struct device *dev)
{
	return device_add_groups(dev, hi5622_cfg11_groups);
}

void hi5622_cfg80211_del_attrs(struct device *dev)
{
	device_remove_groups(dev, hi5622_cfg11_groups);
}
