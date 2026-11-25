# wifi-re-report - the consolidated Wi-Fi / chip-protocol reverse-engineering report

**Lane:** wifi-re-report (shared task `task-172` **v7**; v6 was `task-157`, v5 the `task-129`
addendum, v4 the task-129 original, v3 `task-120`, v2 `task-116`, v1 `task-109`), host-only
consolidation, 2026-11-25. **Status:** publication-grade umbrella report of the whole Hi5622V100
Wi-Fi/chip-protocol investigation, **v7**. v7 integrates and closes the **final RX-hunt wave** (the
append-only ledger's 2026-11-26 00:00-19:00 entries): the AP is on the air (antennas attached, heard
at **-32 dBm**; an external observer saw our WPA2 beacons and a **WPA3-SAE** beacon with
MFP-required/BIP-CMAC-256), TX and management TX are proven (MAC TX `COMPLETED`; the corrected HCC
g0 id6 carrier accepted), and the **one open bug is the device-side RX capture**. The hunt closed
decisively negative on the host side: **R0 parity** reads the vendor stock mirror exactly (five
device-owned live-bank words DEVICE_OK, config block 21/21); **R2 observe-only capture** shows the
MAC receives the client's authentication frames (status `0x182 -> 0x582`, bit 10) but writes
**nothing** to host memory (`frames=0`) and the receive path intermittently faults the device;
the **stock control** proves the device fine (the same laptop authenticated+associated with the
stock vendor AP on the same chip); the **address transform is identity** (no patch shipped); the
**completion-phase prepublish** ticks status bit 2 (`0x182 -> 0x186`) but `frames=0`; the **vendor
WOC buffer class** loads correctly but `frames=0`. The vendor Wi-Fi module is **unstripped with
17,059 symbols**, which enabled the **vendor architecture decode**: the init chain
`hmac_main_init -> hdpp_main_init -> hdpp_chip_init/hdpp_var_init`, the generic **g0 id1
config-event** message format (16-byte HCC header: `[+6]=1` id, u16 sub-event at `[+20]`, len at
`[+22]`, payload at `[+24]`), the `g_ast_*_config_table` registries, 82 message senders and the
firmware's `rx_ring_reset_req`/`enable_trx_req` handler strings - pointing at the **firmware-side
RX configuration/init** (F1) as the remaining cause. The v6 chain is retained throughout: the final
association module (56 params, `ab9e18a4...`), the corrected management-TX/RX carriers, the sibling
GPL/public research and the R11/RF analysis. The per-lane reports under
`opensource/docs/incoming/wifi-*` remain the detail sources and the evidence of first record; this
lane only consolidates, cross-checks and narrates.

**Read in this order:**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the umbrella narrative: executive summary, system map, boot chain, **thirty critical findings (F1-F30)**, the operational loop + calibration + cfg80211 + real-frame + scan + AP + air-TX sessions, the static-RE wave, the one-command matrix, the final all-patch one-boot validation, the v6 association wave, the **v7 final RX-hunt wave** (R0/R2, stock control, the two negative candidate fires, the vendor architecture decode and the F1 lead), the ETE verdict, bench evidence chain, corrections register (**C1-C71**), limitations, reproduction, provenance, lane map, glossary |
| [TABLES.md](TABLES.md) | the data tables behind the narrative: address maps, ROM/boot, protocol groups/ids, wire frames, patch recipes, verification reads, timing, the bench-image/flash pipeline, cfg80211/calibration/MAC/PD4, real frames, E1, matrix, AP start + hostapd gates, air-TX ladder, the v5 tables T32-T38, the v6 tables T39-T45 (association module, g0 id6 mgmt-TX carrier, g1 id13 mgmt RX, sibling/public comparison, RX arm ordering, R11 sweep, fired-results + RF gate) and the **v7 tables T46-T49** (R0 parity/R2 capture, the stock control/PD4 discriminator, the two candidate fires, the vendor architecture decode) - **T1-T49** |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim-by-claim index (**EV-001..EV-372**): every hardware-verified and every static claim with its marker, source artifact and ledger entry |
| [REPRODUCE.md](REPRODUCE.md) | how to re-derive the static claims and how to re-run the bench ladders and the one-command matrix (safety rules included) |
| [tools/verify_report.py](tools/verify_report.py) | device-free verifier: checks this report's cross-references, markers, claim IDs, tables, findings and key constants against the committed lane sources and the ledger (**358/358 checks PASS**) |

**Ground rules honoured.** Host-only: no device, UART, TFTP or network contact was made while
writing this lane. No vendor binary, object code, firmware byte, calibration value, ROM dump or
secret is committed here; firmware/ROM identities appear as SHA-256 only, and the vendor blobs stay
at their local paths. Vendor-derived facts carry the project evidence markers `[V]` / `[V*]` /
`[I]` / `[U]` (defined in REPORT.md section 0.3). The v7 vendor-architecture facts are
addresses/symbol names/structure layouts read from the local module set, never bytes copied.

**Scope note.** This is an interoperability-research document, not a product specification. Two
firmware patches (PD3/PD4) are part of the research method and are marked as research artifacts
throughout; they are not proposed as shipping changes, and with H1 confirmed PD4 is only a
consume-only mailbox servicer (and the stock firmware refuses our H2D without it - v7's stock
control). The cfg80211 bench image, the frame-provider modules, the phase matrix, the hostapd/AP
ladders and the static-lane/RX-hunt patches are likewise research/bench instruments (bench-only, not
in the luofu patch series). The v6 wave proved the association-chain software and fired the corrected
carriers; the v7 wave proved the RF/on-air side (WPA2 + WPA3-SAE beacons seen externally) and closed
the host-side RX question - but the **device-side RX capture is still open** (firmware-mediated,
F1), so a real client association/4-way PTK is still unproven. It is not a working client AP and not
an upstreamable Wi-Fi stack; the firmware remains closed (PD4-patched research image) and the vendor
GPL request is unanswered.

Reproduce the report's own checks (no device, no vendor blob required):

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
cd opensource/docs/incoming/wifi-re-report
$PY tools/verify_report.py            # report cross-check selftest (358/358 PASS at v7)
```
