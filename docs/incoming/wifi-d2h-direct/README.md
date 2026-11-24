# wifi-d2h-direct - direct-read D2H receive service (task-89, host-only)

The firmware's D2H HCC pipeline is alive in our golden boot: it stages the
frames as 8-byte nodes in the **device** group-A DR rings (PKTRAM) and the
frame bodies in the device ACP SRAM.  The ETE engine that would mirror them
into host DRAM stays idle (the firmware-side copy trigger never runs and no
host register feeds it), but the host can read both structures **directly**
through the mapped BAR0 windows.  This lane turns that discovery into a
compile-ready receive service integrated into the `hi5622_pciet_hcc` module:
ring poll + 12-byte HCC header decode + delivery of the g4 id1/id2 ready pair,
EXCP_INFO and the rest into a kernel catalogue/hexdump surface, with the
consumer-advance question answered (the device +0x1C is engine-only and not
needed; the host-owned +0x38 republish is the only vendor-analog index write).

**Start here:**

| document | why |
| --- | --- |
| REPORT.md | the five answers: service design, consumer advance, engine idle, D2H catalogue, hcc-service correction |
| BENCH-PROCEDURE.md | the attended read-only bench ladder, expected observables, stop rules |
| BUILD-NOTES.md | build result, toolchain, patch hunks, selftest |
| src/ | contract header + service + the exact hcc-service integration patch |
| tools/ | deterministic patch generator, module build driver, 92-check device-free selftest (also a raw-hex node/frame decoder) |

Quick host-only verification (no device, no image build):

    ../../../../.provision-venv/bin/python tools/mkpatch.py --check
    ./tools/build.sh
    ../../../../.provision-venv/bin/python tools/d2h_selftest.py

Status: host-only lane; nothing here has been run on the device.  No vendor
firmware/object bytes are committed.  The bench run is the Lead's attended
session.
