# wifi-re-report - the consolidated Wi-Fi / chip-protocol reverse-engineering report

**Lane:** wifi-re-report (shared task `task-116`; v1 was `task-109`), host-only consolidation,
2026-11-25. **Status:** publication-grade umbrella report of the whole Hi5622V100 Wi-Fi/chip-protocol
investigation, **v2**. v2 integrates the later 2026-11-25 milestones: the cfg80211 bench image and
its flash, wiphy registration + nl80211 VAP lifecycle (M0/M1), calibration ladder C1, the ETE-wake
negative, the mapped MAC data plane and the PD4 research-patch audit. The per-lane reports under
`opensource/docs/incoming/wifi-*` remain the detail sources and the evidence of first record; this
lane only consolidates, cross-checks and narrates.

**Read in this order:**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the umbrella narrative: executive summary, system map, boot chain, ten critical findings, the operational loop + calibration + cfg80211 interface + data-plane architecture + ETE verdict, bench evidence chain, corrections register, limitations, reproduction, provenance |
| [TABLES.md](TABLES.md) | the data tables behind the narrative: address maps, ROM map, golden-state, protocol groups/ids, wire frames, patch recipes, verification reads, timing constants, plus the v2 image/flash pipeline, cfg80211 map, calibration ladder, MAC ring map and PD4 audit tables (T1-T25) |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim-by-claim index (EV-001..EV-185): every hardware-verified and every static claim with its marker, source artifact and ledger entry |
| [REPRODUCE.md](REPRODUCE.md) | how to re-derive the static claims and how to re-run the bench ladders (safety rules included) |
| [tools/verify_report.py](tools/verify_report.py) | device-free verifier: checks this report's cross-references, markers, claim IDs and key constants against the committed lane sources |

**Ground rules honoured.** Host-only: no device, UART, TFTP or network contact was made while
writing this lane. No vendor binary, object code, firmware byte, calibration value, ROM dump or
secret is committed here; firmware/ROM identities appear as SHA-256 only, and the vendor blobs
stay at their local paths. Vendor-derived facts carry the project evidence markers
`[V]` / `[V*]` / `[I]` / `[U]` (defined in REPORT.md section 0.3).

**Scope note.** This is an interoperability-research document, not a product specification.
Two firmware patches (PD3/PD4) are part of the research method and are marked as research
artifacts throughout; they are not proposed as shipping changes. The cfg80211 bench image and its
kernel config are likewise research instruments (bench-only, not in the luofu patch series), and
the M0/M1 Wi-Fi interface is a management model, not a working link.

Reproduce the report's own checks (no device, no vendor blob required):

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
cd opensource/docs/incoming/wifi-re-report
$PY tools/verify_report.py            # report cross-check selftest
```
