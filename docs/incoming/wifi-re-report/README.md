# wifi-re-report - the consolidated Wi-Fi / chip-protocol reverse-engineering report

**Lane:** wifi-re-report (shared task `task-129` **v5 addendum**; v4 was the task-129 original, v3
`task-120`, v2 `task-116`, v1 `task-109`), host-only consolidation, 2026-11-29. **Status:**
publication-grade umbrella report of the whole Hi5622V100 Wi-Fi/chip-protocol investigation, **v5**.
v5 integrates the 2026-11-28 **static-RE wave** (five host-only lanes) and the bench fires that
turned its predictions into hardware facts: the **MAC RX live-bank correction** with `dp_rx_arm`
proving the RX consumer (exact vendor mirror), the **WPA2-AP GTK install proven** (key_add +
set_default_key consumed; PTK RX-gated), the **five-message vendor AP-start order** (channel
`0x0124` first, VAP start after `ap_cfg`) reaching `AP-ENABLED`, the **full 13-channel scan closed
as structurally impossible** (clean negative result; ceiling 6 channels/request, 7-8 with H1
pacing), the **STA/5 GHz/HT-VHT model** decoded with a fail-closed patch, the **MAC TX completion
proven** via the vendor TXQ-table queue-state publish (POST-ONLY solved), the **E2 AP restart in one
boot** and a **40-minute AP soak (242/242 samples golden)**. The wave culminated in the **final
all-patch module** - one clean-room module carrying every feature, validated in one boot (vendor
five-message AP order + WPA2-AP GTK + TX COMPLETED + RX arm all PASS; stripped 308,080 B sha256
`5b587949...`, 47 parameters). The H1 verdict (PD4 is consume-only),
the on-air limits (no beacon/association proof yet), and the upstream verdict are unchanged. The
per-lane reports under `opensource/docs/incoming/wifi-*` remain the detail sources and the evidence
of first record; this lane only consolidates, cross-checks and narrates.

**Read in this order:**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the umbrella narrative: executive summary, system map, boot chain, twenty critical findings (F1-F20), the operational loop + calibration + cfg80211 + real-frame + scan + AP + air-TX sessions, the static-RE wave (RX/WPA2/AP-order/scan/STA), the one-command matrix, the static-wave wave and the final all-patch one-boot validation, the ETE verdict, bench evidence chain, corrections register (C1-C56), limitations, reproduction, provenance, lane map, glossary |
| [TABLES.md](TABLES.md) | the data tables behind the narrative: address maps, ROM/boot, protocol groups/ids, wire frames, patch recipes, verification reads, timing, the bench-image/flash pipeline, cfg80211/calibration/MAC/PD4, real frames, E1, matrix, AP start + hostapd gates, air-TX ladder, and the v5 tables T32-T38 (RX live bank/completion, WPA2 key protocol, five-message AP order, scan ceiling, STA/5GHz/HT, fired-results summary, final all-patch one-boot validation) - T1-T38 |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim-by-claim index (EV-001..EV-308): every hardware-verified and every static claim with its marker, source artifact and ledger entry |
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
matrix, the hostapd/AP ladders and the v5 static-lane patches are likewise research/bench
instruments (bench-only, not in the luofu patch series). The v5 wave proves device-level RX/TX
consumption and the WPA2 GTK, but the **on-air** proof still needs an external observer and the AP's
management data path (host mgmt TX + MAC RX) is not implemented - it is not a working client AP and
not an upstreamable Wi-Fi stack; the firmware remains closed (PD4-patched research image) and the
vendor GPL request is unanswered.

Reproduce the report's own checks (no device, no vendor blob required):

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
cd opensource/docs/incoming/wifi-re-report
$PY tools/verify_report.py            # report cross-check selftest
```
