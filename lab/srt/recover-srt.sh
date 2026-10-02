#!/bin/sh
# Self-recovery for the phase-21 SR-trigger test (task st_01a0fbc2).
#
#   recover-srt.sh            -> restore the vendor stack and reboot now
#   recover-srt.sh --watch    -> detached watchdog: wait WATCH seconds, then
#                                  recover unless /tmp/omo-srt.done exists
#
# It puts the vendor modules back, removes the one-shot srt loader and the
# staged module, clears the tmp copies, then reboots into the normal vendor
# stack.  The watchdog is armed with start-stop-daemon before anything is
# staged; it is cancelled by posting the done-flag and running the recovery by
# hand.
WATCH="${WATCH:-1800}"
LOG=/tmp/omo-srt.timer.log

if [ "$1" = "--watch" ]; then
	echo "watchdog armed $(date -u) pid=$$ wait=${WATCH}s" > "$LOG"
	sleep "$WATCH"
	if [ -f /tmp/omo-srt.done ]; then
		echo "watchdog cancelled: /tmp/omo-srt.done present $(date -u)" >> "$LOG"
		exit 0
	fi
	echo "watchdog FIRING: no /tmp/omo-srt.done, running recovery $(date -u)" >> "$LOG"
fi

set -x
cd /lib/modules/$(uname -r) || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-srt
rm -f /etc/init.d/omo-srt
rm -f /lib/modules/$(uname -r)/srt.ko
rm -f /tmp/srt.ko
rm -f /tmp/omo-srt
rm -f /root/recover-srt.sh
sync
reboot
