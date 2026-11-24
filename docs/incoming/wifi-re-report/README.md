# wifi-re-report - the consolidated Wi-Fi / chip-protocol reverse-engineering report

**Lane:** wifi-re-report (shared task `task-129`; v3 was `task-120`, v2 `task-116`, v1 `task-109`),
host-only consolidation, 2026-11-27. **Status:** publication-grade umbrella report of the whole
Hi5622V100 Wi-Fi/chip-protocol investigation, **v4**. v4 integrates the 2026-11-26 08:20 -
2026-11-27 06:30 milestones: the **first real firmware scan results** (mode-1 VAP base; six channel
reports + six beacons, later 12 BSS entries decoded), the **one-command all-PASS matrix**
(boot/C2/D1/D2/E1), the first data-plane frame **published to the MAC TX ring** (POST-ONLY), the
**hostapd capability ladder** (ucode, `.connect`, `.mgmt_tx`, `mgmt_stypes`, `REPORTS_OBSS`, the
`wiphy_new_nm` WARN and the firmware VAP-base crash root cause), **THE FIRST LIVE WI-FI AP ON OUR
CLEAN-ROOM STACK** (`wlan0: AP-ENABLED`, SSID `cudy-ap-test`, four AP-start messages consumed by the
firmware), and the **flash of the ABI-correct image** (kernel slot read-back + ABI receipt matched;
the AP reproduced on the flashed image from rootfs-native modules). The H1 verdict (PD4 is
consume-only) and the honest limits are unchanged. The per-lane reports under
`opensource/docs/incoming/wifi-*` remain the detail sources and the evidence of first record; this
lane only consolidates, cross-checks and narrates.

**Read in this order:**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the umbrella narrative: executive summary, system map, boot chain, fourteen critical findings (F1-F14), the operational loop + calibration + cfg80211 interface + real-frame sessions + scan results + AP milestone + air-TX + one-command matrix + ETE verdict, bench evidence chain, corrections register (C1-C44), limitations, reproduction, provenance, lane map, glossary |
| [TABLES.md](TABLES.md) | the data tables behind the narrative: address maps, ROM map, golden-state, protocol groups/ids, wire frames, patch recipes, verification reads, timing constants, the bench-image/flash pipeline, cfg80211/calibration/MAC/PD4 tables, the real-frame map, the data-plane E1 table, the phase-matrix automation, and the v4 tables T29 (AP start + readback), T30 (hostapd gates) and T31 (air-TX ladder) - T1-T31 |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim-by-claim index (EV-001..EV-277): every hardware-verified and every static claim with its marker, source artifact and ledger entry |
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
consume-only mailbox servicer. The cfg80211 bench image, the frame-provider modules, the phase
matrix and the hostapd/AP ladders are likewise research/bench instruments (bench-only, not in the
luofu patch series). The v4 milestone is a **control-plane** first: the AP comes up on the flashed
bench image with a firmware VAP object and hostapd `AP-ENABLED`, but no beacon has been observed
over the air, no client has associated, and the MAC data plane has only published its first frame
(POST-ONLY). It is not a working link and not an upstreamable Wi-Fi stack; the firmware remains
closed (PD4-patched research image) and the vendor GPL request is unanswered.

Reproduce the report's own checks (no device, no vendor blob required):

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
cd opensource/docs/incoming/wifi-re-report
$PY tools/verify_report.py            # report cross-check selftest
```
