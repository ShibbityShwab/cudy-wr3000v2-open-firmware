#!/bin/sh
# Independent device-side watchdog for the phase-21 live-binding test.
# Started with start-stop-daemon --background so it survives the SSH session.
# After 900 s it runs the recovery unless /tmp/omo-bind.done was posted.
echo "watchdog armed $(date -u)" > /tmp/omo-bind.timer.log
sleep 900
if [ -f /tmp/omo-bind.done ]; then
	echo "watchdog cancelled: /tmp/omo-bind.done present $(date -u)" >> /tmp/omo-bind.timer.log
	exit 0
fi
echo "watchdog FIRING: no /tmp/omo-bind.done, running recovery $(date -u)" >> /tmp/omo-bind.timer.log
/root/recover-bind.sh
