# wifi-rom-flow — firmware boot flow & ROM/dispatcher contract (image-only lane)

Host-only reconstruction of the Hi5622V100 firmware boot flow, the ROM/dispatcher contract and the
init stages the image expects to be invoked from outside the image. Read `REPORT.md` first.

- Result: `REPORT.md` (stage table §4; missing-stage analysis §8).
- Tool: `tools/fwdis.py` — reads the local firmware blob in place (never copies it) and prints
  address-mapped disassembly, literal cross-references, pointer/table dumps and pattern scans.
  Requires capstone 5.x (installed in the repo `.provision-venv`).

Quick start:

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=$PWD/tools/fwdis.py
$PY $T info
$PY $T dis 0xC6E14 0x19C thumb        # common exception handler
$PY $T dis 0x47628 0x1C0 thumb        # boot task prelude
$PY $T w   0x10F240 48                # registration table + crash-dump table
$PY $T ctx 0x0017D398 thumb           # literal-load xrefs
```

Address conventions (same as REPORT §1): CPU = file + 0x40000; host BAR0 (CPU RAM) = CPU + 0x6B8000;
host BAR0 (IO block) = 0x3B8000 + (CA − 0x40000000); host upload = 0x1240000 + file.

Constraints: no device contact, no builds, no vendor bytes committed — this directory contains only
markdown and the analysis tool.
