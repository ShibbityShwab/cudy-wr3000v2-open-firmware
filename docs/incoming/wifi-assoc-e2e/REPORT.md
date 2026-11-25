# REPORT - wifi-assoc-e2e lane (shared task-158, relaunch)

**Lane:** assoc-e2e2. **Host-only.** Docs + host scripts + host tests; the lane
never contacted the bench router. **Date:** 2026-11-25.
**Deliverable root:** opensource/docs/incoming/wifi-assoc-e2e/.

## 1. What was delivered

| artifact | purpose |
| --- | --- |
| RUNBOOK.md | the attended E2E procedure: router rungs M1-M4 + host phases C0-C4 + association rungs A1-A4, with the corrected order (module -> boot -> C2 -> **AP first** -> dp_probe -> dp_setup -> txq_addr_sync -> **dp_rx_arm** -> drain loop) |
| MATRIX.md | PASS/FAIL matrix per rung (M0-M6, A1-A4), client-stage classification table, RF gate thresholds, evidence checklist |
| DIAGNOSIS.md | 11 symptom classes (S1-S11) with the expected evidence and next action; keeps the "no CRX = RF/client; CRX no response = mgmt TX; assoc no EAPOL = data_tx/ring" rule and adds the key/data/host/image classes |
| README.md | lane summary, corrected-module identity, quick start, measured host facts |
| configs/profiles.json + 3 wpa_supplicant templates | open / WPA2-PSK / WPA3-SAE (SAE pwe=2, PMF required) for the AP fixtures; PSK substituted at run time from ASSOC_PSK (lab values only) |
| scripts/assoc_e2e.py | the client reliability harness: preflight, reset (NM/supplicant/p2p/EBUSY class), scan with 3 strategies + RSN verification, one-supplicant assoc with status+log polling and stage classification, watch (antenna alignment RSSI CSV), cleanup/restore, verify |
| scripts/router/assoc-router-fire.sh | the router-side stages with fail-closed guards and golden checks (Lead runs it on the router; never executed here) |
| scripts/verify-association-module.sh | host identity/provenance verifier for the corrected .ko (size/sha/params/feature strings/carrier absence/patch reproduction) |
| tests/ + tests/fixtures.py | 23 RF-free unit tests over the parsers/classifiers/templates/hygiene and CLI dry-runs |
| evidence/ | committed host-test output (no hardware claims) |

## 2. Verification performed (host-only, reproducible)

All commands run from opensource/docs/incoming/wifi-assoc-e2e/:

1. **Unit tests**: python3 tests/test_assoc_e2e.py -> **Ran 23 tests, OK**
   (evidence/unit-tests.txt).
2. **Harness self-check**: python3 scripts/assoc_e2e.py verify ->
   **41/41 PASS** (evidence/host-selftest.txt). Covers iw scan parsing
   (freq/RSSI/DS/RSN AKM/pairwise/MFP), PASS/WARN/FAIL assessment for the
   open/WPA2/WPA3 profiles, Supplicant classification for 7 terminal shapes,
   template rendering/redaction, profiles, and the no-non-lab-credential scan.
3. **Live read-only host preflight**: python3 scripts/assoc_e2e.py preflight
   (no root, no RF change, no router contact) -> **PREFLIGHT PASS** on this
   workstation: MT7922 phy3, 2437 MHz advertised, two-managed-interface
   combination, tools present (evidence/host-preflight.txt).
4. **Corrected module identity/provenance**:
   ./scripts/verify-association-module.sh -> **RESULT: PASS**
   artifact 344,372 B sha256 ab9e18a4..., 56 params; all feature strings
   present; old WAL-0x01F2 carrier string ABSENT; the sibling carrier patch
   applied to the association tree reproduces **5/5 patched files byte for
   byte** (evidence/module-verify.txt).
5. **Router script**: sh -n clean; full --full --dry-run on the host prints the
   complete stage sequence and writes nothing (evidence/router-script-dryrun.txt).
6. **Safety/hygiene**: git-tracked lane contains no vendor binaries/blobs and no
   non-lab credential-like values (enforced by the harness scanner and a unit
   test); live evidence is written only to the gitignored build/ tree.

Not verified here (by design, the Lead fires): every router stage, the RF/air
path, the association itself. The matrix is intentionally NOT pre-filled.

## 3. Measured facts the runbook relies on (from committed project evidence)

* Corrected mgmt-TX carrier accepted on hardware: HCC g0 id6 control message
  (12-B header + 48-B device TX control + inline frame), posted 86 B, consumed
  0 polls; completion DMAC 0x01F3 is not decoded by the driver; mgmt RX is D2H
  CRX group1 id13 and is delivered to cfg80211.
* RX live bank is armed only in the order AP-first -> dp_setup -> rx_arm:
  status=0x182, mask=0xfffffcc1, norm/small rptr=0x0020; with dp_setup before
  the AP the status is 0x02 (cleared).
* AP start order (hardware-proven): vap_add -> ap_chan (0x0124, 32 B band=0
  ch6) -> ap_ssid -> ap_cfg -> ap_vap_start.
* Host RF baseline without antennas: AP BSSID 06:05:04:03:02:01, ch6,
  -63..-65 dBm in wlan0 scans; -62 dBm in a monitor beacon capture. Prior
  host attempt failure modes: stale supplicant/EBUSY scan; wlan1 passive scans
  missed the AP while wlan0 scans/monitor saw it.
* The MT7922 wiphy allows two managed interfaces / two channels, so the
  non-disruptive wlan1 client is legal; --use-base remains the fallback.

## 4. Limits (honest)

* This lane did not fire anything. The association/4-way is the next bench
  result; RUNBOOK/MATRIX define how to capture it, they do not claim it.
* One AP profile per boot is prescribed: AP start re-initializes the MAC and
  the RX arm is one-shot per load, so a profile change after the DP chain
  would leave the RX bank unverifiable. This is stricter than the E2 restart
  capability on purpose.
* The WPA3-SAE rung is image-dependent (hostapd needs CONFIG_SAE); the runbook
  records "invalid key_mgmt SAE" as an image blocker, not a driver failure.
* The client harness trusts wpa_supplicant status/log for the client stage;
  the router snapshot is the authoritative other half of every rung.
* WLAN1 scan reliability is improved (3 strategies + retries) but not proven
  until the live fire; the fallback to the base interface is documented.

## 5. Ledger entry (append to the repo ledger by the Lead - this lane commits
## only inside opensource/ per instruction)

    ## 2026-11-25 13:30 - ASSOCIATION E2E LANE DELIVERED (assoc-e2e2, task-158;
    opensource docs/incoming/wifi-assoc-e2e/): the client-side reliability +
    association runbook for the antenna-test fire. (1) CLIENT HARNESS
    scripts/assoc_e2e.py implements the canonical sequence that the failed
    attempt was missing: preflight -> reset (kill stale supplicants, NM
    unmanaged, optional second managed vif, down/up settle, power_save off)
    -> scan (active single-channel -> passive -> full-band, with RSN/SSID/
    BSSID/RSSI-gate assessment) -> ONE dedicated wpa_supplicant (p2p_disabled,
    freq_list=2437) with status+log polling -> stage classification
    (COMPLETED / AUTH-TIMEOUT / SAE-AUTH-TIMEOUT / ASSOC-TIMEOUT /
    FOURWAY-FAILED / DISCONNECTED / NETWORK-NOT-FOUND / DRIVER-ERROR) ->
    evidence JSON + cleanup/restore; watch mode for antenna RSSI alignment.
    (2) wpa_supplicant fixtures for the open/WPA2/WPA3-SAE AP profiles
    (published lab values only, staged 0600 outside the repo). (3) ROUTER
    script scripts/router/assoc-router-fire.sh encodes the corrected order
    module -> boot ladder -> C2 -> AP FIRST -> dp_probe -> dp_setup ->
    h2d_txq_addr_sync -> dp_rx_arm -> drain loop, with golden page/mask/
    staging checks after every rung; the Lead runs it, the lane never does.
    (4) RUNBOOK + PASS/FAIL MATRIX (M0-M6, A1-A4, RF gate -70/-55 dBm) +
    DIAGNOSIS (S1-S11: no CRX=RF/client, CRX no response=mgmt TX, assoc no
    EAPOL=data TX/RX/ring, then keys/data/host/image classes). (5) Corrected
    module identity/provenance verifier: ab9e18a4 344,372 B / 56 params /
    g0 id6 carrier verified, sibling patch reproduces 5/5 patched files;
    unit tests 23/23, self-check 41/41, live read-only host preflight PASS.
    NOT fired (host-only lane). NEXT: antennas + the fire; fill MATRIX.md.

## 6. Handoff notes for the Lead

* Run the host verifier + tests BEFORE the fire (section 2); do not load the
  old WAL-0x01F2 artifact.
* The current bench session (AP on air, corrected mgmt TX accepted, RX armed
  status 0x182, page golden) already matches RUNBOOK R2/R3 if the AP profile for
  this boot is the one being tested; if it is not, reboot with the target
  profile rather than restarting hostapd after the DP chain.
* Capture the router snapshot immediately after every client attempt; the
  client JSON alone cannot distinguish mgmt-TX from RX failures.
* If the host harness reports cannot-create-wlan1 (-EBUSY), rerun with
  --use-base; cleanup restores NetworkManager.
