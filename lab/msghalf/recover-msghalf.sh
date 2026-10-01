#!/bin/sh
# Recovery for the phase-20c hostwin test.
# Restores the vendor modules, removes the one-shot loader, the staged module
# and the tmp copy, then reboots into the normal vendor stack.
set -x
cd /lib/modules/5.10.201 || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-msghalf
rm -f /etc/init.d/omo-msghalf
rm -f /lib/modules/5.10.201/msghalf.ko
rm -f /tmp/msghalf.ko
sync
reboot
