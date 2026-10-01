#!/bin/sh
# Recovery for the phase-21 service-thread (svc) test.
# Restores the vendor modules, removes the one-shot loader, the staged module
# and the tmp copy, then reboots into the normal vendor stack.
set -x
cd /lib/modules/5.10.201 || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-srpump
rm -f /etc/init.d/omo-srpump
rm -f /lib/modules/5.10.201/srpump.ko
rm -f /tmp/srpump.ko
rm -f /root/recover-srpump.sh
sync
reboot
