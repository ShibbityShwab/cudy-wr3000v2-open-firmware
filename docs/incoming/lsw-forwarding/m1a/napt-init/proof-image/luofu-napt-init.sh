#!/bin/sh /etc/rc.common
# Probe-only boot-time NAPT/flow init replay for the native CPU-delivery proof.
# Runs only when the kernel command line carries luofu_napt_init=1. Register-only;
# table bases are in the switch flow carveout (0x80400000, outside Linux RAM).
START=11

rd() { devmem "$1" 32 2>/dev/null; }
wr() { devmem "$1" 32 "$2" >/dev/null 2>&1; }

boot() {
	case "$(cat /proc/cmdline)" in
	*luofu_napt_init=1*) ;;
	*) exit 0 ;;
	esac
	[ -x /usr/bin/devmem ] || exit 0
	# already initialised (vendor bases present): nothing to do
	now=$(rd 0x12280130)
	case "$now" in
	0x80400000) exit 0 ;;
	esac
	wr 0x1200002c 0x10000034
	wr 0x12280070 0x00110138
	wr 0x12280064 0x00000001
	wr 0x12280054 0x00000100
	wr 0x12280130 0x80400000
	wr 0x12280138 0x80408000
	wr 0x12280134 0x80448000
	wr 0x1228013c 0x80450000
	wr 0x12280068 0x00020303
	i=0
	for cmd in 0x000a0001 0x00080001; do
		wr 0x12280060 $cmd
		i=0
		while [ "$i" -lt 200 ]; do
			v=$(rd 0x12280060)
			[ $((v & 1)) -eq 0 ] && break
			i=$((i + 1))
		done
	done
	wr 0x12280050 0x00f00b69
	echo "luofu-napt-init: applied (up_port=$(rd 0x1200002c) bases=$(rd 0x12280130)/$(rd 0x12280138))" > /dev/kmsg
}
