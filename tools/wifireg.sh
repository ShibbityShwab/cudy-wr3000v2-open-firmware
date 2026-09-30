#!/bin/sh
set -eu
CA=$(printf '%d' "$1")
EP=${2:-0}
COUNT=${3:-1}
case "$EP" in
	0) BAR=0x40000000 ;;
	1) BAR=0x58000000 ;;
	*) echo "endpoint must be 0 or 1" >&2; exit 2 ;;
esac
# host = BAR + 0x3b8000 + (CA - 0x40000000); reading outside this window faults the PCIe bus
i=0
while [ "$i" -lt "$COUNT" ]; do
	ADDR=$((BAR + 0x3b8000 + CA - 0x40000000 + i * 4))
	printf 'ca=%#x host=%#x value=' "$((CA + i * 4))" "$ADDR"
	devmem "$ADDR"
	i=$((i + 1))
done
