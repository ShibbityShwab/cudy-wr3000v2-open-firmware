// SPDX-License-Identifier: GPL-2.0
/*
 * abiprobe: print the struct offsets that decide whether an external module
 * built against vanilla linux-5.10.201 headers can safely write kernel
 * structures the vendor kernel allocated.
 *
 * It exists to make the CONFIG_PM delta in struct wiphy visible: the
 * wowlan pointer pair is the only field group between interface_modes and
 * bands[] in include/net/cfg80211.h, and it is #ifdef CONFIG_PM. With
 * CONFIG_PM=y (vanilla multi_v7_defconfig) bands[] sits 8 bytes higher than
 * with CONFIG_PM=n (what the vendor kernel behaves like), so a module built
 * with PM on writes the wrong slot.
 *
 * Build the same source twice - once with the tree's default CONFIG_PM=y and
 * once after lab/abi-pm-off.sh has forced CONFIG_PM=n in the prepared headers -
 * and compare the two dmesg lines. No hardware, no device access, no state.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>

static int __init omo_abiprobe_init(void)
{
#ifdef CONFIG_PM
	const char *pm = "y";
#else
	const char *pm = "n";
#endif
#ifdef CONFIG_PM_SLEEP
	const char *pmsleep = "y";
#else
	const char *pmsleep = "n";
#endif

	pr_info("omo-abiprobe: CONFIG_PM=%s CONFIG_PM_SLEEP=%s\n", pm, pmsleep);
	pr_info("omo-abiprobe: wiphy: sizeof=%zu interface_modes=%zu bands=%zu "
		"perm_addr=%zu hw_version=%zu\n",
		sizeof(struct wiphy),
		offsetof(struct wiphy, interface_modes),
		offsetof(struct wiphy, bands),
		offsetof(struct wiphy, perm_addr),
		offsetof(struct wiphy, hw_version));
	pr_info("omo-abiprobe: net_device: sizeof=%zu name=%zu state=%zu "
		"ifindex=%zu netdev_ops=%zu\n",
		sizeof(struct net_device),
		offsetof(struct net_device, name),
		offsetof(struct net_device, state),
		offsetof(struct net_device, ifindex),
		offsetof(struct net_device, netdev_ops));
	return 0;
}

static void __exit omo_abiprobe_exit(void)
{
}

module_init(omo_abiprobe_init);
module_exit(omo_abiprobe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("offset probe for the vanilla/vendor struct-ABI delta (CONFIG_PM)");
