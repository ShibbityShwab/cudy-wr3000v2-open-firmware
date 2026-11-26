# wifi-rx-smac-complete - the SMAC PPDU-completion producer of the firmware group-2 id-3 (RX-complete) message

**Lane:** `wifi-rx-smac-complete` (shared task **task-221**). **HOST-ONLY static decode** of the
pinned `FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...c3ef311b`; **CA = file offset + 0x40000**).
No device contact, no flash, no fire in this lane. **The Lead fires.**

This lane closes the last link of the F1: the previous lane (`wifi-rx-mac-delivery`, task-220)
decoded the **consumer** half - the group-2 id-3 dispatch entry `0xBBC9C`, the per-frame
decoder `0xBA1C8`, the CRX builder `0xB9780` and the PPDU descriptor re-arm `0xB52F0` - and
left "who posts the group-2 id-3 message?" unresolved. It is now found and byte-verified:

**the per-band SMAC interrupt body `0xB53A4`** (band 0 IRQ 49 / band 1 IRQ 55), reached from the
ARM IRQ vector at CA `0x40064` -> event dispatcher `0xC2EFC` -> handler table `0x17D430` ->
wrapper `0xB54C8` / `0xB54BC` -> per-band object from RAM `0x180814+8/+0x1C`.

It is the **only** site in the whole image that builds a group-2/id-3 HCC message (all 56
`hcc_msg_tx` callers were signature-scanned, [V]) and the payload fields it writes
(`hdr+0x14` = band object, `hdr+0xc` = received-frame chain) are **exactly** the two fields the
decoded consumer `0xBBC9C` reads [V]. The remaining gap to "counters move" is now a **gate
question with named probes**, not an unknown entry: see [REPORT.md](REPORT.md) sec. 5-7,
[PROBES.md](PROBES.md) and [FIRE-RECIPE.md](FIRE-RECIPE.md).

## Files

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | verdict, the producer decode, the dispatch chain, preconditions, host-visible state, why the counters are 0, escalation |
| [TABLES.md](TABLES.md) | every IRQ -> wrapper -> body row, the producer's instruction-level sequence, host conversion rules |
| [PROBES.md](PROBES.md) | read-only probe pack P0-P11 (INTC proxies, SMAC status/ack words, counters, MAC masks) |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the one attended read pass, the decision table, and the escalation rungs |
| [tools/fw_rx_smac_complete_re.py](tools/fw_rx_smac_complete_re.py) | stdlib re-derivation tool, `--selftest` **197/197 PASS** |
| [evidence/](evidence/) | selftest, decode, tables and probes output |

## Reproduction

```sh
# from the repo root (finds rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin)
.provision-venv/bin/python opensource/docs/incoming/wifi-rx-smac-complete/tools/fw_rx_smac_complete_re.py --selftest
.provision-venv/bin/python opensource/docs/incoming/wifi-rx-smac-complete/tools/fw_rx_smac_complete_re.py --decode
.provision-venv/bin/python opensource/docs/incoming/wifi-rx-smac-complete/tools/fw_rx_smac_complete_re.py --probes
```

Grades used everywhere: **[V]** re-derived by the committed tool; **[V\*]** committed earlier
lane/bench; **[I]** inference; **[U]** needs a device read. CA = file offset + 0x40000.
