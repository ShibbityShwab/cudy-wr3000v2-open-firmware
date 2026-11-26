# rxdisp2 - the F1's firmware local-dispatch/CRX chain (C5/C6/C7)

**Lane:** `rxdisp2` (task-242). **HOST-ONLY static decode** of the pinned vendor Wi-Fi
firmware (`rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256
`7fc87e20...c3ef311b`, **CA = file offset + 0x40000**). No device contact, no writes, no
vendor bytes tracked. Tool: [tools/fw_rxdisp2_re.py](tools/fw_rxdisp2_re.py) `--selftest`
**73/73 PASS**; every CA/field/expected value below is asserted there.

**One-line result.** The C5/C6/C7 split is now decoded instead of inferred:
(1) the local queue-1 notifier and the customer worker are installed **unconditionally by the
same per-destination init body that creates the HCC RX thread** - the thread that dispatches
every H2D message this bench already uses - so a *NULL* q1 notifier is not a state our boot can
be in; (2) the transport queue q0 (C7) is proven live by the g1 id3 config replies the host
receives; (3) the only gates left are the CRX builder's **per-frame RX-descriptor gates** inside
`0xB9780` (chip RX-ctx `[chip+0x1A00]`, class nibble, forward/dest resolution, len cap), i.e.
properties of the received frame, not of the firmware's queue wiring.

## Contents

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode, the re-ranking of C5/C6/C7, the fix candidates |
| [TABLES.md](TABLES.md) | instruction/data tables and the dest0 runtime-wiring map |
| [PROBES.md](PROBES.md) | read-only probe pack P0/P12-P19 (prior) + P20-P23 (new expected values) |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the attended read-only pass and the decision matrix |
| [tools/](tools/) | `fw_rxdisp2_re.py` - the re-derivation/selftest tool |
| [evidence/](evidence/) | selftest/decode/tables/probes output (regenerable) |

## Regenerate

```
cd <repo-root>
.provision-venv/bin/python opensource/docs/incoming/rxdisp2/tools/fw_rxdisp2_re.py --selftest
.provision-venv/bin/python opensource/docs/incoming/rxdisp2/tools/fw_rxdisp2_re.py --decode
.provision-venv/bin/python opensource/docs/incoming/rxdisp2/tools/fw_rxdisp2_re.py --tables
.provision-venv/bin/python opensource/docs/incoming/rxdisp2/tools/fw_rxdisp2_re.py --probes
```

The tool opens the vendor image in place (never copies it) and emits no firmware bytes.

## Provenance / inputs

* `wifi-rx-mac-delivery` (task-220): consumer `0x10CC18[id3] -> 0xBBC9C -> 0xBA1C8 ->
  0xB9780`; `wifi-rx-smac-complete` (task-221): the only g2/id3 builder is `0xB53A4`;
  `wifi-rx-d2h-post` (task-226): `0x102760` = per-group queue enqueue + notifier, the q1/Q0
  routing, probe pack P12-P19; `wifi-rx-f1-thread` (task-216): the thread table and the
  "HCC RX Thread = H2D service" fact; `wifi-rx-crx-gating` (task-164): the CRX builder gate
  matrix G1-G10.
* New here: the queue/customer binding and worker decode (C5), the chip+0x1A00 installer, the
  builder's class-nibble gate `0xB9864`, the exact dest-nibble resolution `0xBC524` at
  `0xB97A0`/`0xBA108`, the g2/id2 report-class call at `0xBA408-0xBA48A`, and the static
  expected values for q0/q1/q2 notifiers and customer1 (P20-P23).

**Safety:** read-only loads only; never write CA `0x400392F0`; never read the RC misc window
`0x10160000..0x1016FFFF`; no retries on a failed width; one coherent boot per tuple.
