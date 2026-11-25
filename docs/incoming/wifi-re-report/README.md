# wifi-re-report - the consolidated Wi-Fi / chip-protocol reverse-engineering report

**Lane:** wifi-re-report (shared task `task-157` **v6**; v5 was the `task-129` addendum, v4 the
task-129 original, v3 `task-120`, v2 `task-116`, v1 `task-109`), host-only consolidation,
2026-11-25. **Status:** publication-grade umbrella report of the whole Hi5622V100 Wi-Fi/chip-protocol
investigation, **v6**. v6 integrates the final association wave (ledger tail 2026-11-25
11:30-13:00): the **final association module** (all-patch + wpa3-sae + ap-mgmt + sta-ops + data-tx;
**56 parameters**, stripped 344,472 B sha256 `251d08f0...`; rebuilt with the corrected carrier as
**344,372 B sha256 `ab9e18a4...`**); the **corrected device management-TX carrier** - a raw HCC
**g0 id6** control message (12-B header + 48-B device TX control + inline frame; completion DMAC
**0x01F3** -> host-internal g2 id13), hardware-accepted (posted **86 B**, consumed **0 polls**), with
the WAL `0x01F2` TLV identified as host-internal and the ap-mgmt M2 ladder withdrawn; **management RX
= D2H CRX `g1 id13`** (60-B prefix, frame at +48; ring-empty-during-scan is expected); the
**sibling-GPL/public-research** analysis (CRX/DRX split and producer-as-RX-enable confirmed; the
Hi3881 `SET_RX_FILTER` has **no luofu sender** - do not port; the scan cap is DMAC/firmware-side; no
public exact-chip source and the Cudy 2.0 GPL section is empty); the **RX-arm ordering correction**
(AP-start first -> `dp_setup` -> `rx_arm`; status **0x182** vs **0x02**); the **final R11 scan sweep**
(**8 channels max** at `field10=25`, others 6-7; 13 structurally unreachable); and the **RF gate**
(~44 dB path loss with no antennas, AP TX ~-64 dBm at 30 cm, marginal client RX + flaky MT7922). The
v5 chain is retained below/throughout: MAC RX live-bank correction, WPA2-AP GTK, five-message AP
order, 13-channel-scan negative, STA/5 GHz/HT-VHT model, MAC TX completion, E2 restart and the
40-minute soak, culminating in the **final all-patch module** (stripped 308,080 B sha256
`5b587949...`, 47 params, one-boot validation). The H1 verdict (PD4 is consume-only), the on-air
limits (no client association/4-way PTK yet - RF-gated) and the upstream verdict are unchanged. The
per-lane reports under `opensource/docs/incoming/wifi-*` remain the detail sources and the evidence
of first record; this lane only consolidates, cross-checks and narrates.

**Read in this order:**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the umbrella narrative: executive summary, system map, boot chain, twenty-seven critical findings (F1-F27), the operational loop + calibration + cfg80211 + real-frame + scan + AP + air-TX sessions, the static-RE wave (RX/WPA2/AP-order/scan/STA), the one-command matrix, the static-wave wave and the final all-patch one-boot validation, the **v6 association wave** (association module, corrected g0 id6 mgmt-TX carrier, g1 id13 mgmt RX, sibling/public research, RX ordering, R11 sweep, RF gate), the ETE verdict, bench evidence chain, corrections register (C1-C64), limitations, reproduction, provenance, lane map, glossary |
| [TABLES.md](TABLES.md) | the data tables behind the narrative: address maps, ROM/boot, protocol groups/ids, wire frames, patch recipes, verification reads, timing, the bench-image/flash pipeline, cfg80211/calibration/MAC/PD4, real frames, E1, matrix, AP start + hostapd gates, air-TX ladder, the v5 tables T32-T38 (RX live bank/completion, WPA2 key protocol, five-message AP order, scan ceiling, STA/5GHz/HT, fired-results summary, final all-patch one-boot validation) and the **v6 tables T39-T45** (final association module, corrected g0 id6 mgmt-TX carrier, g1 id13 mgmt RX, sibling/public comparison, RX arm ordering, R11 sweep, v6 fired-results + RF gate) - T1-T45 |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim-by-claim index (EV-001..EV-342): every hardware-verified and every static claim with its marker, source artifact and ledger entry |
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
consumption and the WPA2 GTK; the v6 wave adds the association-chain software (keys, station ops,
EAPOL/data TX, corrected mgmt TX/RX carriers) and fires the corrected carrier, but a real client
association/4-way PTK is still unproven - it is **RF-gated** (no antennas on the bench: ~44 dB path
loss) - and the on-air proof still needs a reliable external client/observer. It is not a working
client AP and not an upstreamable Wi-Fi stack; the firmware remains closed (PD4-patched research
image) and the vendor GPL request is unanswered.

Reproduce the report's own checks (no device, no vendor blob required):

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
cd opensource/docs/incoming/wifi-re-report
$PY tools/verify_report.py            # report cross-check selftest
```
