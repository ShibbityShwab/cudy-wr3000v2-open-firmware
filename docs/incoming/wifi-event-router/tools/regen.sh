#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Regenerate the derived annotations used by REPORT.md.  Reads the LOCAL vendor
# firmware blob and modules in place; never copies them; never opens a device.
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
PY="$ROOT/.provision-venv/bin/python"
T="$ROOT/opensource/docs/incoming/wifi-event-router/tools"
FW=/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
PLAT=/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko
WIFI=/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko
export HI5622_FW="$FW"

echo "== dispatcher / init / raise / enable / timer / handshake =="
"$PY" "$T/devreg.py" dis 0xC2EFC 0xA0
"$PY" "$T/devreg.py" dis 0xC3024 0xA0
"$PY" "$T/devreg.py" dis 0xC74B0 0xA0
"$PY" "$T/devreg.py" dis 0xC1A3C 0x60
"$PY" "$T/devreg.py" dis 0xC6FF4 0x60
"$PY" "$T/devreg.py" dis 0xC6DB8 0x60
"$PY" "$T/devreg.py" dis 0x46848 0xE0
"$PY" "$T/devreg.py" dis 0xC704C 0x110
"$PY" "$T/devreg.py" dis 0xC6C88 0x60
"$PY" "$T/devreg.py" dis 0x473CC 0x90
"$PY" "$T/devreg.py" dis 0x477EC 0x50
"$PY" "$T/devreg.py" dis 0x496C8 0x170
"$PY" "$T/devreg.py" dis 0x120A0C 0x60

echo "== device-register census (0x4016 / 0x40036 / 0x40039 / 0x4010 blocks) =="
"$PY" "$T/devreg.py" census --lo 0x40160000 --hi 0x401A0000
"$PY" "$T/devreg.py" census --lo 0x40036000 --hi 0x40037000
"$PY" "$T/devreg.py" census --lo 0x40039000 --hi 0x4003A000
"$PY" "$T/devreg.py" defs --lo 0x40160000 --hi 0x40162000

echo "== vendor modules: event-controller constants =="
"$PY" "$T/ko_devca.py" scan --ko "$PLAT"
"$PY" "$T/ko_devca.py" scan --ko "$WIFI"

echo "== next bench action (addresses only) =="
"$PY" "$T/evctrl_probe.py"
