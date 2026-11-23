# wifi-ete-engine lane (shared task task-82)

Definitive host-only answer to **"what starts the Hi5622V100 ETE engine, and can any host-visible
action wake it?"** - the last gap in the clean-room HCC transport after the firmware boots and
self-configures its DR source rings.

**Lane:** `ete-engine`, **host-only** (2026-11-23). No device contact, no image build, no vendor
byte/binary/firmware content committed - only derived addresses, constants and structure layouts.
Local inputs: the vendor firmware image and the 2.5.24 `hi5622v100_plat.ko` / `hi5622v100_wifi.ko`
(read in place, never copied).

## Deliverables

* [REPORT.md](REPORT.md) - full reversal:
  * every static `{CA,value}` init table in the image (the only genuine one is the 31+31-entry
    PHY/MAC register table at CPU 0x1058B8/0x1059B0 with its apply loop at 0xA0184) and proof that
    **no table touches the ETE window**;
  * the `hcc_instance` (CA 0x10F1F0), DR/SR ring records, ETE ops table CA 0x10CEDC and the
    ETE register-window map (status 0x4003A874 / ack 0x4003A870 / engine blocks
    0x4003A540+0x4003A720 / doors 0x4003A7C0+0x4003A7D0 / send words 0x4003AC00+ / 0x4003AC18+);
  * **every image writer of the ETE block** and a whole-image negative proving nothing writes the
    status word, send words, fills, doors or the global area;
  * the deposit -> ring-commit -> `d2h_notify` -> flush chain and the **sending-trigger**
    (CPU 0xC625A) chain that is the only ETE kick in the firmware;
  * the vendor-module comparison (the vendor host has no engine-start write either);
  * the definitive verdict: **no host-visible action can wake the engine** - the missing state is
    ROM/first-stage-internal (entry-A inter-core/ROM handshake CA 0x1BF008 == 2, event delivery
    S13, and the ETE engine-global registers that no image or host code writes), with the exact
    next reconstruction and the evidence that would prove it;
  * the read-only bench snapshot pack (BAR0 list) and one bounded diagnostic write class.
* [tools/ete_engine.py](tools/ete_engine.py) - reproducible verifier/recon:
  `selftest` (150/150 PASS: every [V] fact in the report), `tables`, `layout`, `writers`,
  `chain`, `benchpack` (read-only BAR0 read list; never opens a device).
* [tools/fwdis.py](tools/fwdis.py) - the project's recursive-descent Thumb disassembler, copied
  read-only for cross-checks.

## Reproduce

    PY=<repo>/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ete-engine/tools
    $PY ete_engine.py selftest     # 150/150 PASS
    $PY ete_engine.py tables       # {CA,value} init-table census + PHY apply loop + crash-dump table
    $PY ete_engine.py layout       # hcc_instance / g / ETE window map
    $PY ete_engine.py writers      # image ETE writers + whole-image negatives
    $PY ete_engine.py chain        # deposit -> commit -> notify -> flush -> sending trigger
    $PY ete_engine.py benchpack    # read-only bench read list

## Bottom line in one paragraph

The ETE engine is not started by any register: the firmware's only kick is the sending trigger
CPU 0xC625A, invoked from the device-ETE status dispatcher (event 0x2E) and fed by the hardware-
authored status word 0x4003A874 and send words 0x4003ACxx, whose only possible origin is the
device ETE hardware itself. The host (and the vendor host driver) own only the mirror ring fields
and the host-facing interrupt mask, which B3 already provides vendor-exactly; the image writes
only ring configuration/enables and a status mask. In this boot the ROM-only event-delivery path
is dead (entry A spins on the ROM/inter-core handshake CA 0x1BF008 == 2, so the image never
unmasks IRQ and the ROM IRQ trampoline 0x40064 -> 0xC2EFC never dispatches), so the engine is
never commanded. Next work is host-only-recoverable only as evidence: a golden vendor-boot diff of
the firmware's own ETE crash-dump region {0x4003A000, 0xC28} plus CA 0x1BF008, and the
reconstruction of the ROM stage S0/S1 handshake and ETE engine-global init - not more host writes.
