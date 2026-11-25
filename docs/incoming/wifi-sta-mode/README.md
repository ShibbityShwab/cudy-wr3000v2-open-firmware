# wifi-sta-mode - STA (client) connect flow: join + host-SME auth/assoc + CRX completion

**Lane:** `wifi-sta-mode` (shared task **task-155**), **host-only**.
**Written:** 2026-11-25. **Parents:** `wifi-sta-static` (task-141) ->
`wifi-sta-join` (task-146), on top of the FINAL ASSOCIATION module
(`wifi-stability`, task-136) and the corrected mgmt-TX carrier
(`wifi-sibling-gpl`, task-151).
**Status:** implementation + host tests + full compile-verify **PASS**;
bench ladder **designed, not fired** (the Lead fires). No vendor
binary/firmware/calibration byte and no secret is committed; the pinned
vendor module is read-only local evidence only.

## Why

The router-as-a-Wi-Fi-client end state needs four things that did not exist
in one module before: (1) the hardened 108-byte WAL cfg-event `0x123` join
post (wifi-sta-join), (2) the device-facing management-TX carrier for the
authentication/association requests (the corrected HCC g0 id 6 message from
wifi-sibling-gpl - the old WAL `0x01F2` carrier was host-internal and never
left the host), (3) a decoder/consumer for the association completion on the
**CRX management path** (the received auth/assoc response frames, D2H chip
group 1 id 13), and (4) the cfg80211 connect completion
(`cfg80211_connect_done`) plus a bounded, fail-closed connect state machine.
This lane integrates all four into one deterministic host-SME flow and
delivers the ladder that proves it end-to-end on the bench.

## Headline results

1. **The client-association completion is a CRX (control-RX) event, decoded.**
   The pinned vendor driver is a split-MAC host SME: the STA FSM table
   {`scan_rsp`, `join_rsp`, `auth_rsp`, `asoc_rsp`} at `.rodata+0x8074` is
   reached from `hmac_rx_process_mgmt_event` (D2H **group 1 id 13**), and
   `hmac_handle_asoc_rsp_sta` (`.text+0x99040`) parses the received 802.11
   association response and reports the connect result to cfg80211 [V]. The
   on-air assoc response - delivered by our already-decoded CRX path - is the
   completion event; the driver calls `cfg80211_connect_done()` with the
   locally-held req_ie/resp_ie and never dereferences a firmware-supplied IE
   address.
2. **Correction to the inherited completion model:** the WAL chip-group-2 id
   3 message decoded by wifi-sta-join is the vendor's *host-internal* WAL
   relay / device state-sync family (the same handler shape the device uses
   for the full-MAC alternative), not a data-ring event. This lane keeps that
   decoder as a diagnostic, but completes on the CRX response path.
3. **Full STA host-SME flow implemented, fail-closed, one connect per load:**
   WAL `0x123` join post (hardened, DMA-window device addresses) -> 802.11
   authentication request on the corrected g0 id 6 mgmt carrier -> CRX auth
   response -> association request -> CRX assoc response ->
   `cfg80211_connect_done(status=0, bssid, req_ie, resp_ie)`; rejection and
   timeout complete as failures; `.disconnect` reports
   `cfg80211_disconnected()`. Reference-locked module parameters:
   `sta_connect` (0 fail-closed default / 1 dry / 2 live, read-only),
   `sta_connect_timeout_ms` (bounded 500..60000).
4. **Frame layouts host-tested:** auth request 30 B (open system), association
   request 28+SSID+rates IEs, auth/assoc response decoders with strict
   subtype/length/IE-boundary checks, and the decoded H2D association-state
   sync (group 2 id 3, 12-B header + 28-B payload) builder - deliberately
   **not posted** until the firmware receive-table entry is proven.
5. **Verification:** host selftests **140/140 PASS** (strict C99
   `-Wall -Wextra -Werror -pedantic`); full module build **0 compiler/modpost
   warnings**; debug-stripped artifact **364,572 B sha256
   `8c0adf93...`** (unstripped 1,396,116 B), vermagic
   `6.18.60 SMP mod_unload ARMv7 p2v8`, **58 module parameters**, cfg80211
   provider symvers **14 lines** (adds `cfg80211_connect_done`,
   `cfg80211_disconnected`; both imported by the .ko).
6. **EAPOL/data for STA mode needs no new code:** the 4-way EAPOL exchange
   rides the inherited data path (EAPOL is a normal data frame to the netdev;
   TX = `ndo_start_xmit`/dp TX ring, RX = `dp_rx_drain`/MAC completion ring,
   both delivered by the wifi-data-tx / wifi-rx lanes). The ladder rung after
   association is therefore a data-path test, not a new carrier.

## Deliverables

| path | content |
| --- | --- |
| [REPORT.md](REPORT.md) | full analysis: split-MAC host-SME model, CRX completion decode, implemented flow, gates, corrections, open items |
| [TABLES.md](TABLES.md) | T1-T12: evidence anchors, FSM table, frame layouts, response offsets, state matrix, params, CRX layout, H2D sync, gates, artifact identity |
| [TEST-LADDER.md](TEST-LADDER.md) | topology (controlled second AP / host AP), host rungs H0-H3, bench rungs B0-B6 + stop rules; **not fired** |
| [patches/0001-sta-mode-connect-crx.patch](patches/0001-sta-mode-connect-crx.patch) | 8 files / 1354 insertions on top of the FINAL ASSOCIATION tree + carrier patch + sta-join patch |
| [src/](src/) | portable `hi5622_sta_sme.{h,c}` (FSM/frames/decoders) and kernel glue `hi5622_sta_mode.{h,c}` reference copies |
| [tools/sta_mode_selftest.c](tools/sta_mode_selftest.c) | 140 host checks (frame bytes, decoders, FSM transitions, sync frame) |
| [tools/build-sta-mode-module.sh](tools/build-sta-mode-module.sh) | reconstructs the chain, builds, asserts identity/params/symvers, runs the selftests |
| [evidence/](evidence/) | reproduction output (build+selftest log, artifact identity, patch apply) |

## Quick reproduction

```sh
# host selftests only (no device, no vendor bytes):
gcc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
    -o /tmp/sta_mode_selftest tools/sta_mode_selftest.c src/hi5622_sta_sme.c
/tmp/sta_mode_selftest                        # PASS: 140/140

# full chain + module build (host-only; needs the project 6.18.60 tree):
./tools/build-sta-mode-module.sh
ASSOC_TREE=/tmp/wifi-assoc-build/integ ./tools/build-sta-mode-module.sh
```

## Limits

* The module is host-built, **NOT fired**; the bench ladder in
  [TEST-LADDER.md](TEST-LADDER.md) is the first on-air validation.
* Client association is currently RF-limited on the bench (~44 dB loss with
  no antennas, flaky laptop MT7922); the user is attaching antennas. The
  ladder is ordered so the management/join path can be exercised against an
  external observer before a data-plane rung is expected.
* The firmware receive-table entry for the H2D association-state sync
  (g2 id 3) is [U]; that builder is host-tested only and not posted.
* WPA2/WPA3 key installation and the 4-way handshake are the inherited key /
  data-path lanes' behaviour, exercised by the post-association rungs.
