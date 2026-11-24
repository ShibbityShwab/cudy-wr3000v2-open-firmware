# wifi-ete-wake lane (shared task task-106)

Host-only lane answering: **what starts the Hi5622v100 ETE engine's group A->B copy, which
part of that trigger (if any) does the host own, and what bench ladder can still close it?**
The full duplex HCC protocol already works on the clean-room host; the device->host direction
is served today by the direct read of the device group-A rings because the engine copy never
runs (status CA 0x4003A874 = 0, send words 0, DR fills static).

**Lane:** `ete-wake` (task-106), host-only, 2026-11-25. No device contact, no image build, no
vendor byte/binary/firmware content committed - only derived addresses, constants and
structure layouts. Local inputs (read in place, never copied): the vendor firmware blob
`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B, sha256
`7fc87e20...ef311b`) and the 2.5.24 `hi5622v100_plat.ko`.

## Deliverables

* [REPORT.md](REPORT.md) - the definitive trigger-chain reversal, the corrections to the
  working premises (status-bit/channel mapping, callback identities, host-owned fields), the
  host-enabler delta table, the ranked experiment ladder and the verified negative boundaries.
* [BENCH-PROCEDURE.md](BENCH-PROCEDURE.md) - the exact attended ladder (R0 read-only pack,
  E1a/E1b host-owned `+0x38` writes, E2 golden-vendor diff), stop rules and recovery gates.
* [tools/ete_wake.py](tools/ete_wake.py) - reproducible verifier/recon: `selftest` asserts
  every [V] fact (81 checks); `chain`, `dispatchers`, `install`, `raise`, `host`, `bench` print the
  derived annotations and the read pack. Reads the local blob in place; never opens a device.
* [tools/hcc_dr_pub.patch](tools/hcc_dr_pub.patch) - the compile-verified bench-only driver
  patch that adds the read-only `hcc_ete_wake` snapshot and the `hcc_dr_pub` E1a/E1b attribute (against the wifi-h2d-send tree;
  build evidence in BENCH-PROCEDURE.md). No driver tree is modified by this lane.
* [recipes/dr-pub-writes.json](recipes/dr-pub-writes.json) - the exact E1 write rows (CA,
  BAR0 offset, candidate values, preconditions) for the bench tooling/audit.

Patch compile evidence (host-only, /tmp build): module 411,228 B, sha256
`9351305b...478196`, 0 compiler/modpost warnings, vermagic 6.18.60 SMP mod_unload ARMv7
p2v8; `patch -p1 --dry-run` clean against the committed wifi-h2d-send tree.

## Reproduce

    PY=<repo>/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ete-wake/tools
    $PY ete_wake.py selftest      # 81/81 PASS
    $PY ete_wake.py chain         # trigger chain 0x4003A874 -> event 0x2E -> 0xC2EFC -> 0x4624C -> 0xC625A
    $PY ete_wake.py dispatchers   # both dispatcher bodies decoded with literals
    $PY ete_wake.py install       # pcie_msg_init callback/event install sites
    $PY ete_wake.py raise         # whole-image event-raise census (0x2D/0x2E never raised)
    $PY ete_wake.py host          # vendor-host ETE comparison (no DR kick; +0x38/+0x18 re-arm only)
    $PY ete_wake.py bench         # the R0 read pack with CA + BAR0 offsets

## Bottom line in one paragraph

The group A->B copy is started by **device ETE hardware**, not by the firmware at will and not
by the host: the hardware raises one of the status bits **24-28** of CA 0x4003A874 when a DR
channel needs service, the ROM-vectored event dispatcher delivers it as **event 0x2E** to the
image handler **0x4624C**, which acks the upper status bits into CA 0x4003A870 and calls
`hcc_instance+0x1C` = the **sending trigger CPU 0xC625A(hcc, channel)**; that trigger reads the
hardware-authored send word (CA 0x4003AC00+4*ch), handshakes the target index into the PKTRAM
control word (record+0x14 = staging+0x100) and releases the tagged 0x5A5A nodes - which is what
commands the copy. No image code and no vendor-host code can author the status bits or the send
words, the image never polls the status word, and event 0x2D/0x2E are never software-raised
(all [V], fresh disassembly). Every host-owned ETE field the vendor stack programs is already
vendor-exact in the clean driver **except one state delta**: the DR group-B published index is
armed 0x400 by our driver while the vendor's own `pcie_ete_dr_init`/`dr_reg_init` publish **0**
and a working vendor boot shows `fill == published` on every DR channel (3/3, 0x404/0x404).
The ladder therefore tests that single host-owned delta first (E1a/`+0x38 := fill`; E1b
`+0x38 := 0`), then closes with a read-only golden-vendor window diff (E2). If E1 does not wake
the engine, the verdict is definitive: the start condition is device/ROM-internal state (engine
global/door words the host cannot author) and the direct group-A read stays the supported
receive path, with its documented limits.