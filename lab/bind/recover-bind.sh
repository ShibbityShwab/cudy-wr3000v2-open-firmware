#!/bin/sh
# Recovery for the phase-21 live-binding test (task st_01a0fb83).
# Restores the vendor modules, removes the one-shot bind loader, the staged
# module, the probe-arming aid and the tmp copies, then reboots into the
# normal vendor stack.
set -x
cd /lib/modules/$(uname -r) || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-bind
rm -f /etc/init.d/omo-bind
rm -f /etc/rc.d/S08omo-livebind
rm -f /etc/init.d/omo-livebind
rm -f /lib/modules/$(uname -r)/bind.ko
rm -f /tmp/bind.ko
rm -f /tmp/omo-bind
rm -f /root/recover-bind.sh
sync
reboot
