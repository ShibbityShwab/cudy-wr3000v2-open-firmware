// SPDX-License-Identifier: GPL-2.0
/*
 * wifiskel: register a cfg80211 wiphy from a module we built ourselves,
 * on the vendor 5.10.201 kernel, with no hardware behind it.
 *
 * This is the struct-ABI test that matters for a real Wi-Fi driver. The
 * phase-11 hwprobe finding showed that struct pci_dev is laid out for our
 * vanilla build's config and the vendor kernel's layout differs, so a module
 * that dereferences kernel structs reads the wrong offsets. cfg80211 is the
 * deepest integration a wireless driver does: wiphy_new_nm hands us a
 * struct wiphy that cfg80211 (the vendor's code) allocated, and we fill in
 * fields (bands[], interface_modes) that the vendor's wiphy_register then
 * reads back. If our struct wiphy / ieee80211_supported_band / ieee80211_channel
 * offsets differ from the vendor kernel's, wiphy_register either rejects the
 * wiphy or the registered phy shows wrong data.
 *
 * There is deliberately NO hardware here: no PCI access, no MMIO, no netdev,
 * no data path. It only exercises the cfg80211 registration plumbing.
 *
 * On load:  wiphy_new_nm() -> fill band -> wiphy_register()
 * On unload: wiphy_unregister() -> wiphy_free()
 * Every step logs its return code to dmesg with the "omo-skel" prefix.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <net/cfg80211.h>

#define OMO_WIPHY_NAME	"omo-skel"

static struct wiphy *omo_wiphy;

/*
 * No interfaces are ever created (we never call cfg80211 to add one), so no
 * cfg80211 callback can be reached. An empty ops table keeps the module
 * minimal and still passes wiphy_new_nm()'s consistency WARN_ONs.
 */
static const struct cfg80211_ops omo_ops = {
};

/* channels 1..13, 20 MHz, nominal 20 dBm */
#define OMO_CHAN(_ch) {				\
	.center_freq = 2407 + (_ch) * 5,	\
	.hw_value = (_ch),			\
	.max_power = 20,			\
}

static struct ieee80211_channel omo_2ghz_channels[] = {
	OMO_CHAN(1),  OMO_CHAN(2),  OMO_CHAN(3),  OMO_CHAN(4),
	OMO_CHAN(5),  OMO_CHAN(6),  OMO_CHAN(7),  OMO_CHAN(8),
	OMO_CHAN(9),  OMO_CHAN(10), OMO_CHAN(11), OMO_CHAN(12),
	OMO_CHAN(13),
};

/* minimal, sane CCK + OFDM set (bitrate in 100 kbps), as mac80211_hwsim uses */
static struct ieee80211_rate omo_2ghz_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20,  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55,  .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 },
	{ .bitrate = 90 },
	{ .bitrate = 120 },
	{ .bitrate = 180 },
	{ .bitrate = 240 },
	{ .bitrate = 360 },
	{ .bitrate = 480 },
	{ .bitrate = 540 },
};

static struct ieee80211_supported_band omo_2ghz_band = {
	.channels	= omo_2ghz_channels,
	.n_channels	= ARRAY_SIZE(omo_2ghz_channels),
	.bitrates	= omo_2ghz_rates,
	.n_bitrates	= ARRAY_SIZE(omo_2ghz_rates),
	/* HT bits kept minimal: 1 spatial stream (MCS0-7), 20 MHz + SGI */
	.ht_cap		= {
		.ht_supported	= true,
		.cap		= IEEE80211_HT_CAP_SGI_20 |
				  IEEE80211_HT_CAP_MAX_AMSDU,
		.ampdu_factor	= IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density	= IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs		= {
			.rx_mask	= { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
			.tx_params	= IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

static int __init omo_wifiskel_init(void)
{
	struct wiphy *wiphy;
	int rc;

	pr_info("omo-skel: init: built against vanilla 5.10.201 headers: "
		"sizeof(struct wiphy)=%zu sizeof(struct ieee80211_supported_band)=%zu "
		"sizeof(struct ieee80211_channel)=%zu sizeof(struct ieee80211_sta_ht_cap)=%zu\n",
		sizeof(struct wiphy),
		sizeof(struct ieee80211_supported_band),
		sizeof(struct ieee80211_channel),
		sizeof(struct ieee80211_sta_ht_cap));

	wiphy = wiphy_new_nm(&omo_ops, 0, OMO_WIPHY_NAME);
	pr_info("omo-skel: wiphy_new_nm(ops, sizeof_priv=0, name=\"%s\") rc=%d ptr=%px\n",
		OMO_WIPHY_NAME, wiphy ? 0 : -ENOMEM, wiphy);
	if (!wiphy)
		return -ENOMEM;

	omo_wiphy = wiphy;

	wiphy->bands[NL80211_BAND_2GHZ] = &omo_2ghz_band;
	pr_info("omo-skel: set bands[2GHZ] rc=0 (n_channels=%d n_bitrates=%d "
		"ht_supported=%d first_ch=%u max_power=%d)\n",
		omo_2ghz_band.n_channels, omo_2ghz_band.n_bitrates,
		omo_2ghz_band.ht_cap.ht_supported,
		omo_2ghz_channels[0].center_freq,
		omo_2ghz_channels[0].max_power);

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	pr_info("omo-skel: set interface_modes=0x%x rc=0 (no netdev is created)\n",
		wiphy->interface_modes);

	rc = wiphy_register(wiphy);
	pr_info("omo-skel: wiphy_register() rc=%d\n", rc);
	if (rc) {
		wiphy_free(wiphy);
		omo_wiphy = NULL;
		pr_err("omo-skel: registration rejected (rc=%d), wiphy freed\n", rc);
		return rc;
	}

	pr_info("omo-skel: registered wiphy \"%s\": no netdev, no hardware, no data path\n",
		OMO_WIPHY_NAME);
	return 0;
}

static void __exit omo_wifiskel_exit(void)
{
	if (!omo_wiphy) {
		pr_info("omo-skel: unload: nothing was registered\n");
		return;
	}

	wiphy_unregister(omo_wiphy);
	pr_info("omo-skel: wiphy_unregister() rc=0 (void)\n");

	wiphy_free(omo_wiphy);
	pr_info("omo-skel: wiphy_free() rc=0 (void)\n");

	omo_wiphy = NULL;
	pr_info("omo-skel: unloaded, phy removed\n");
}

module_init(omo_wifiskel_init);
module_exit(omo_wifiskel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WR3000 V2.0 cfg80211 wiphy registration skeleton (no hardware)");
