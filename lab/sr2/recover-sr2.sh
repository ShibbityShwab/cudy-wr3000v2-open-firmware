#!/bin/sh
# Self-recovery for the phase-21 sibling-endpoint test (task st_01a0fb98).
#
#   recover-sr2.sh            -> restore the vendor stack and reboot now
#   recover-sr2.sh --watch    -> detached watchdog: wait WATCH seconds, then
#                                  recover unless /tmp/omo-sr2.done exists
#
# It puts the vendor modules back, removes the one-shot sr2 loader and the
# staged module, clears the tmp copies, then reboots into the normal vendor
# stack.  The watchdog is armed with start-stop-daemon before anything is
# staged; it is cancelled by posting the done-flag and running the recovery by
# hand.
WATCH="${WATCH:-1800}"
LOG=/tmp/omo-sr2.timer.log

if [ "$1" = "--watch" ]; then
	echo "watchdog armed $(date -u) pid=$$ wait=${WATCH}s" > "$LOG"
	sleep "$WATCH"
	if [ -f /tmp/omo-sr2.done ]; then
		echo "watchdog cancelled: /tmp/omo-sr2.done present $(date -u)" >> "$LOG"
		exit 0
	fi
	echo "watchdog FIRING: no /tmp/omo-sr2.done, running recovery $(date -u)" >> "$LOG"
fi

set -x
cd /lib/modules/$(uname -r) || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-sr2
rm -f /etc/init.d/omo-sr2
rm -f /lib/modules/$(uname -r)/sr2.ko
rm -f /tmp/sr2.ko
rm -f /tmp/omo-sr2
rm -f /root/recover-sr2.sh
sync
reboot
