# Lab: a repeatable reverse-engineering loop for this device

Point the lab at a firmware's two binaries and it re-derives everything we have learned about the
Wi-Fi stack, in seconds, with stable output files. Run it twice against two firmware versions and diff
the output directories: that is the loop, and it is how changes between vendor releases fall out.

## What it needs

- `python3` with `pyelftools` and `capstone` (the workspace venv at `../pyenv` works: `PY=.../python.exe`).
- Two binaries per firmware version:
  - the Wi-Fi firmware blob: `/lib/firmware/hi_wifi/FIRMWARE.bin` on the device (older releases keep
    the kernel modules in `/lib/hisilicon/ko/`, 2.5.24 and later in `/lib/modules/5.10.201/`),
  - the driver module: `hi5622v100_wifi.ko`.
  Extract both from an official image with `rdsquashfs -c <path> <rootfs.sqfs> > <file>`, or copy them
  off your own device with `scp`.

## Running it

    PY=../pyenv/Scripts/python.exe sh run_all.sh <label> <FIRMWARE.bin> <hi5622v100_wifi.ko>
    PY=../pyenv/Scripts/python.exe sh run_all.sh 2.5.24 /path/FIRMWARE.bin /path/hi5622v100_wifi.ko

Output lands in `out/<label>/`: the symbol map, the dispatcher dump, the command table, the module
census, the MMIO constants, plus logs. `diff -r out/2.4.15 out/2.5.24` is the whole comparison.

## The analyzers

| tool | input | what it produces |
| --- | --- | --- |
| `fw_tables.py` | blob | every `{runtime address, name}` pair in the firmware's tables (file offset = address - 0x40000, odd = Thumb) |
| `fw_dispatch.py` | blob | the `alg` dispatcher's disassembly and its 25-entry `{cfg_id, handler}` table |
| `fw_symbols.py` | blob | symbol map built from all name tables |
| `fw_callgraph.py` | blob | call graph of the named handlers |
| `ko_algtable.py` | module | the 414-entry command table (`name, cfg_id, direction`) |
| `ko_symmap.py` | module | function census by layer prefix and the largest functions |
| `ko_mmio.py` | module | the BAR-range constants the module materializes |
| `kv_analyze.py` | calibration store | container check and the zero-run skeleton |
| `regdump_diff.py` | two register dumps | changed addresses, ranges and per-page classification |

## Worked example: 2.4.15 versus 2.5.24

`docs/phase9/version-diff.md` records the first comparative run. Short version: the Wi-Fi firmware blob
is byte-identical in both releases, while the driver module changed in ways that name themselves.

## Ground rules

- Nothing here writes to hardware. The only device-touching tools are in `../tools/`, and they are
  read-only by default; `docs/HAZARDS.md` lists the two ways to reboot this router by accident.
- Every claim the lab prints should be reproducible from the printed command; if a claim cannot be
  re-derived, it does not belong in a report.
