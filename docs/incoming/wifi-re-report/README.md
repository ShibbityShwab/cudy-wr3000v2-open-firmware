# wifi-re-report - the consolidated Wi-Fi / chip-protocol reverse-engineering report

**Lane:** wifi-re-report (shared task `task-120`; v2 was `task-116`, v1 `task-109`), host-only
consolidation, 2026-11-26. **Status:** publication-grade umbrella report of the whole Hi5622V100
Wi-Fi/chip-protocol investigation, **v3**. v3 integrates the 2026-11-25 16:30 - 2026-11-26 06:30
milestones: the complete six-message calibration boot (C2, automated PASS 54/54), the first real
firmware management frames (VAP `user_add`/`user_add_notify` + the first 520 B scan request, all
consumed in 0 polls; no scan results yet -> the firmware **VAP-start wall**), the MAC data-plane host
implementation with its TXQ-table address sync (automated E1 PASS 37/37), the one-command full
bring-up matrix (boot/C2/D1/D2/E1 = PASS/PASS/PASS/BLOCKED-DRIVER/PASS) and the **H1 verdict** (PD4
is consume-only; the working duplex is the non-mailbox ETE/polled H2D path). The per-lane reports
under `opensource/docs/incoming/wifi-*` remain the detail sources and the evidence of first record;
this lane only consolidates, cross-checks and narrates.

**Read in this order:**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the umbrella narrative: executive summary, system map, boot chain, eleven critical findings (F1-F11), the operational loop + calibration + cfg80211 interface + real-frame sessions + data-plane architecture + one-command matrix + ETE verdict, bench evidence chain, corrections register (C1-C33), limitations, reproduction, provenance |
| [TABLES.md](TABLES.md) | the data tables behind the narrative: address maps, ROM map, golden-state, protocol groups/ids, wire frames, patch recipes, verification reads, timing constants, the v2 image/flash pipeline and cfg80211/calibration/MAC/PD4 tables, plus the v3 real-frame map, data-plane E1 table and phase-matrix automation (T1-T28) |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim-by-claim index (EV-001..EV-244): every hardware-verified and every static claim with its marker, source artifact and ledger entry |
| [REPRODUCE.md](REPRODUCE.md) | how to re-derive the static claims and how to re-run the bench ladders and the one-command matrix (safety rules included) |
| [tools/verify_report.py](tools/verify_report.py) | device-free verifier: checks this report's cross-references, markers, claim IDs, tables, findings and key constants against the committed lane sources and the ledger |

**Ground rules honoured.** Host-only: no device, UART, TFTP or network contact was made while
writing this lane. No vendor binary, object code, firmware byte, calibration value, ROM dump or
secret is committed here; firmware/ROM identities appear as SHA-256 only, and the vendor blobs
stay at their local paths. Vendor-derived facts carry the project evidence markers
`[V]` / `[V*]` / `[I]` / `[U]` (defined in REPORT.md section 0.3).

**Scope note.** This is an interoperability-research document, not a product specification.
Two firmware patches (PD3/PD4) are part of the research method and are marked as research
artifacts throughout; they are not proposed as shipping changes, and with H1 confirmed PD4 is only a
consume-only mailbox servicer. The cfg80211 bench image, the frame-provider module and the phase
matrix are likewise research/bench instruments (bench-only, not in the luofu patch series), and the
M0/M1 + mailbox-VAP interface is a management model with real VAP/scan requests - not a working
link (no scan results, no association, no frame TX/RX yet).

Reproduce the report's own checks (no device, no vendor blob required):

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
cd opensource/docs/incoming/wifi-re-report
$PY tools/verify_report.py            # report cross-check selftest
```
