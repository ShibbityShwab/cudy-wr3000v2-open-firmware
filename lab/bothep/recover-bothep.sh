#!/bin/sh
# Self-recovery for the phase-21b two-endpoint test (task st_01a0fbda).
#
#   recover-bothep.sh            -> restore the vendor stack and reboot now
#   recover-bothep.sh --watch    -> detached watchdog: wait WATCH seconds, then
#                                   recover unless /tmp/omo-bothep.done exists
#
# It puts the vendor modules back, removes the one-shot bothep loader and the
# staged module, clears the tmp copies, then reboots into the normal vendor
# stack.  The watchdog is armed with start-stop-daemon before anything is
# staged; it is cancelled by posting the done-flag and running the recovery by
# hand.
WATCH="${WATCH:-1800}"
LOG=/tmp/omo-bothep.timer.log

if [ "$1" = "--watch" ]; then
	echo "watchdog armed $(date -u) pid=$$ wait=${WATCH}s" > "$LOG"
	sleep "$WATCH"
	if [ -f /tmp/omo-bothep.done ]; then
		echo "watchdog cancelled: /tmp/omo-bothep.done present $(date -u)" >> "$LOG"
		exit 0
	fi
	echo "watchdog FIRING: no /tmp/omo-bothep.done, running recovery $(date -u)" >> "$LOG"
fi

set -x
cd /lib/modules/$(uname -r) || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-bothep
rm -f /etc/init.d/omo-bothep
rm -f /lib/modules/$(uname -r)/bothep.ko
rm -f /tmp/bothep.ko
rm -f /tmp/omo-bothep
rm -f /root/recover-bothep.sh
sync
reboot
