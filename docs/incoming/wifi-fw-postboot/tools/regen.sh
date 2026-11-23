#!/bin/sh
# regen.sh - reproduce every claim of docs/incoming/wifi-fw-postboot/REPORT.md.
# Host-only: reads the local vendor firmware blob and the local ROM dump IN PLACE.
# Never copies vendor bytes into this directory; never opens a device.
set -e
PY=${PY:-/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python}
T=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INCOMING=$(CDPATH= cd -- "$T/../.." && pwd)
ROMDIS=$INCOMING/wifi-rom-dump/tools/romdis.py

echo "== 0. instrument audit: the headline result =="
$PY "$T/regmap_probe.py"

echo
echo "== 1. entry A post-handshake continuation =="
$PY "$T/fwdis.py" dis 0x473CC 0xE0
echo "-- task create and scheduler start"
$PY "$T/fwdis.py" dis 0xC745C 0x60
$PY "$T/fwdis.py" dis 0xC704C 0x180

echo
echo "== 2. event-controller init, FIQ dispatcher, BUG stubs =="
$PY "$T/fwdis.py" dis 0xC3024 0xA0
$PY "$T/fwdis.py" dis 0xC2EEC 0x20
$PY "$T/fwdis.py" dis 0xC19C0 0x40

echo
echo "== 3. slot owner census =="
$PY "$T/fwdis.py" lrefs 0x0017D42C
$PY "$T/fwdis.py" find 0x00047629
$PY "$T/fwdis.py" find 0x00047625

echo
echo "== 4. boot state machine =="
$PY "$T/fwdis.py" dis 0x47620 0x50
$PY "$T/fwdis.py" dis 0x47CE0 0x60
$PY "$T/fwdis.py" dis 0x48140 0x160

echo
echo "== 5. entry B / CPU1 parallel path =="
$PY "$T/fwdis.py" dis 0xC742C 0x30
$PY "$T/fwdis.py" dis 0xC73A0 0xC0

echo
echo "== 6. CPSR usage census (cpsie i sites; expect no cpsie f) =="
$PY - <<'EOF'
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
data = open("/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin", "rb").read()
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
md.skipdata = True
for ins in md.disasm(data, 0x40000):
    if ins.mnemonic in ("cpsie", "cpsid", "msr"):
        print(f"{ins.address:08x}: {ins.mnemonic} {ins.op_str}")
EOF

echo
echo "== 7. ROM side (prior-lane tool, reads the local ROM dump in place) =="
$PY "$ROMDIS" flow
$PY "$ROMDIS" stores
