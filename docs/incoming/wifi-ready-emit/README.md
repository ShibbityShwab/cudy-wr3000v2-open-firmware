# wifi-ready-emit lane (shared task task-80)

Why the firmware never emits the HCC chip-group-4 ready frames (device→host over the
ETE DR rings), and what host-visible action (if any) gates them. **Host-only lane**:
the firmware image and vendor modules are read in place as local evidence; no device
was contacted, no image was built, no vendor byte is committed here.

Deliverables:

* [REPORT.md](REPORT.md) - full reversal: the verified ready/deposit/notify chain, the
  ETE TX/DR two-ring publish model, the 0x86E14 startup/vector contract, the vendor ETE
  TX queue handling, the B3 P0 vs firmware-expectation comparison, the definitive
  no-host-trigger conclusion, the read-only bench decision pack and the single next
  bench action.
* [tools/readyemit.py](tools/readyemit.py) - disassembly + structure assertions
  (`selftest` asserts every fact quoted in the report: 47 checks).
* [tools/bench-reads.py](tools/bench-reads.py) - the READ-ONLY bench snapshot pack with
  BAR0 mapping and the S1/S2/S3 decision matrix. It never opens a device.
* [tools/fwdis.py](tools/fwdis.py) - the boot-gate lane's disassembler, copied here
  read-only for reproducibility.

Reproduce:

    PY=<repo>/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ready-emit/tools
    $PY readyemit.py selftest
    $PY readyemit.py chain
    $PY readyemit.py records
    $PY bench-reads.py --markdown

Bottom line: every host-owned precondition the firmware consumes is already provided
by the B3 transport; the ready-path message reaches the ETE deposit op (bench
`out[1]` bit6 is its notification), but the device-side ETE source→host copy never
runs. No host register/mailbox write feeds that copy; the single next bench action is
the read-only pack in REPORT.md section 7.
