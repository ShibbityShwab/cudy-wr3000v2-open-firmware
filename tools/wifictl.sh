#!/bin/sh
set -eu
BAR0_CA=0x40000000
BAR0_WIN=0x3b8000
DEVMEM="devmem"

usage() {
	cat <<'EOF'
usage: wifictl <command> [args]
  info                      chip id, die id, firmware marker, uptime
  reg <ca> [ep] [count]     read chip registers by config address (ep 0 or 1)
  alg <name> [args...]      run an iwpriv alg command on the 2.4 GHz netdev
  algraw <iface> <name>     same on a named netdev (Hisilicon0, vap0, ...)
  cal <snapdir>             snapshot calibration to a directory (needs the cal toolkit)
  ramdump <addr> <len> <file>
                            dump device CPU memory (loads wifi_debug if needed; reboot to unload)
EOF
}

host_addr() {
	ep=$1
	ca=$2
	case "$ep" in
		0) bar=0x40000000 ;;
		1) bar=0x58000000 ;;
		*) echo "endpoint must be 0 or 1" >&2; exit 2 ;;
	esac
	printf '0x%x' $((bar + BAR0_WIN + ca - BAR0_CA))
}

cmd_info() {
	echo "chip:    $(iwpriv Hisilicon0 get_chipid 2>/dev/null | grep -m1 -o 'chip id:.*' || echo unknown)"
	echo "die:     $(iwpriv Hisilicon0 get_dieid 2>/dev/null | head -2 | tail -1)"
	echo "firmware:$(cat /etc/custom-firmware-version 2>/dev/null || echo unknown)"
	echo "slot:    $(cat /sys/class/ubi/ubi0/mtd_num 2>/dev/null)"
	echo "uptime:  $(uptime | sed 's/^ *//')"
}

cmd_reg() {
	ca=$1
	ep=${2:-0}
	count=${3:-1}
	i=0
	while [ "$i" -lt "$count" ]; do
		addr=$(host_addr "$ep" $((ca + i * 4)))
		printf 'ca=%#x host=%s value=' $((ca + i * 4)) "$addr"
		$DEVMEM "$addr"
		i=$((i + 1))
	done
}

case "${1:-}" in
	info) cmd_info ;;
	reg) [ $# -ge 2 ] || { usage; exit 2; }; shift; cmd_reg "$@" ;;
	alg) [ $# -ge 2 ] || { usage; exit 2; }; shift; iwpriv Hisilicon0 alg "$@" ;;
	algraw) [ $# -ge 3 ] || { usage; exit 2; }; iface=$2; shift 2; iwpriv "$iface" alg "$@" ;;
	cal) [ $# -ge 2 ] || { usage; exit 2; }; sh /usr/bin/wifi-cal-snapshot.sh "$2" ;;
	ramdump)
		[ $# -ge 4 ] || { usage; exit 2; }
		[ -e /sys/hisys/plat/pcie ] || { modprobe wifi_debug 2>/dev/null || insmod /lib/hisys/wifi_debug.ko 2>/dev/null || true; sleep 1; }
		echo "savemem 0 $2 $3 $4" > /sys/hisys/plat/pcie
		echo "wrote $4"
		;;
	*) usage; exit 2 ;;
esac
