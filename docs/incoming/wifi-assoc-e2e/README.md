# wifi-assoc-e2e - client association E2E runbook + client-side reliability (task-158)

**Lane:** assoc-e2e2 (shared task-158, relaunch of the failed task-153).
**Scope:** host-only. This lane writes documents + scripts and runs HOST tests;
it never contacts the bench router (no SSH/UART/TFTP/flash/devmem/module work).
**The Lead fires the bench run.** Everything here is the procedure for that fire.

## What problem this solves

The last hard blocker is a real client associating to the clean-room AP and
completing the 4-way handshake. The software path is implemented end-to-end in
the final association module; what failed in the previous attempt was the
**client side** (flaky MT7922: NetworkManager/supplicant conflicts, stale
p2p-dev state, EBUSY scans, no repeatable sequence) and the **RF** (no antennas
on the bench box, ~44 dB loss). This lane delivers the missing pieces:

1. **One canonical host-client sequence** (reset -> one supplicant -> scan ->
   associate -> classify -> cleanup) as a tested harness,
   [scripts/assoc_e2e.py](scripts/assoc_e2e.py).
2. **wpa_supplicant profiles** for the open / WPA2-PSK / WPA3-SAE AP fixtures
   (published lab values only, generated 0600 outside the repo at run time).
3. **The router-side fire sequence** in the ONE order now proven correct
   (AP first, then dp_setup, then rx_arm) as a guarded, Lead-run script,
   [scripts/router/assoc-router-fire.sh](scripts/router/assoc-router-fire.sh).
4. **The expected evidence per rung** (dmesg lines, hostapd lines, client
   stages, counters) and a **PASS/FAIL matrix**: [MATRIX.md](MATRIX.md), with
   the symptom->cause table in [DIAGNOSIS.md](DIAGNOSIS.md).
5. A **host-side module identity verifier** for the corrected association
   module (g0 id6 mgmt-TX carrier),
   [scripts/verify-association-module.sh](scripts/verify-association-module.sh).

## The corrected module this runbook assumes

The fire loads the **corrected carrier** artifact, not the lane-built
WAL-0x01F2 one:

| field | value |
| --- | --- |
| file | hi5622_wifi.stripped.ko (hardware-accepted 2026-11-25) |
| size | 344,372 B |
| sha256 | ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e |
| params | 56 (all fail-closed defaults; the association run enables the 11 feature params) |
| carrier | HCC group 0 / id 6 control message: 12-B HCC header + 48-B device TX control + inline 802.11 frame (posted 86 B, consumed 0 polls on hardware) |
| completion | device DMAC 0x01F3 -> host-internal g2 id13 relay (NOT decoded by the driver; cfg80211 TX status comes from the post-consumption fact) |
| mgmt RX | D2H group 1 / id 13 CRX events, decoded and delivered to cfg80211_rx_mgmt for the AP wdev |

The .ko is local evidence and is **never committed**. If /tmp was cleaned,
rebuild it with ../wifi-stability/tools/build-association-module.sh plus the
sibling carrier patch
(../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch); the exact
recipe prints with:

    ./scripts/verify-association-module.sh --rebuild

## Quick start (Lead, at the bench)

Host, before touching the router:

    cd opensource/docs/incoming/wifi-assoc-e2e
    ./scripts/verify-association-module.sh               # must print RESULT: PASS
    python3 tests/test_assoc_e2e.py                      # 23 host tests, no RF
    python3 scripts/assoc_e2e.py verify                  # 41 classifier checks

Copy the router-side files to the router (module + one script; the AP fixtures
are already baked into the final image):

    tools/rscp.sh /tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko root@<bench-ip>:/root/hi5622_assoc.ko
    tools/rscp.sh opensource/docs/incoming/wifi-assoc-e2e/scripts/router/assoc-router-fire.sh root@<bench-ip>:/root/assoc-router-fire.sh
    ssh root@<bench-ip> sha256sum /root/hi5622_assoc.ko   # must equal ab9e18a4...

Then run [RUNBOOK.md](RUNBOOK.md) top to bottom. Short form:

    # router console
    sh /root/assoc-router-fire.sh --check
    sh /root/assoc-router-fire.sh --full --load /root/hi5622_assoc.ko --profile wpa2
    # after each client attempt:
    sh /root/assoc-router-fire.sh --stage snapshot

    # host (sudo)
    sudo python3 scripts/assoc_e2e.py preflight
    sudo python3 scripts/assoc_e2e.py reset
    sudo python3 scripts/assoc_e2e.py scan --profile wpa2
    sudo python3 scripts/assoc_e2e.py assoc --profile wpa2 --timeout 60
    sudo python3 scripts/assoc_e2e.py cleanup

## Files

| path | what |
| --- | --- |
| RUNBOOK.md | the attended end-to-end procedure (router rungs R1-R9, host phases C0-C6) |
| MATRIX.md | PASS/FAIL matrix per rung, current expectations, evidence required |
| DIAGNOSIS.md | symptom -> likely cause -> next action (RF, mgmt TX, RX, data, keys) |
| REPORT.md | lane report: verification performed, limits, ledger entry for the Lead |
| configs/profiles.json | the three AP profiles (SSID, security, RSN expectation, hostapd fixture) |
| configs/wpa_supplicant-*.template.conf | client fixtures; PSK token substituted at run time from ASSOC_PSK |
| scripts/assoc_e2e.py | host client harness (preflight/reset/scan/assoc/watch/cleanup/verify) |
| scripts/verify-association-module.sh | host identity/provenance verifier for the corrected .ko |
| scripts/router/assoc-router-fire.sh | ROUTER-side stages (Lead runs it on the router console) |
| tests/ | 23 RF-free host tests + fixtures (synthetic shapes only) |
| evidence/ | committed host-test / verifier / dry-run output (no hardware claims) |

## Host/client facts this lane is built on (measured, not assumed)

* The workstation's Wi-Fi is a single MT7922 (phy3, driver mt7921e) with
  **wlan0** as the 5 GHz host uplink and a P2P-device wdev. Its wiphy
  combinations allow **#{managed} <= 2, total <= 3, #channels <= 2** - a
  second managed interface (wlan1) is legal while wlan0 is connected.
* Prior evidence: scans that saw the bench AP (BSSID 06:05:04:03:02:01 on
  channel 6, 2437 MHz) were run on **wlan0** at **-63..-65 dBm** during the
  no-antenna period; a monitor capture saw the beacon at -62 dBm. Two wlan1
  passive-scan attempts (~2026-11-24/25) returned only the 5 GHz neighbour and
  missed the AP (see build/wifi-onair-observer evidence).
  * Therefore the harness defaults to wlan1 (non-disruptive) but retries the
    scan aggressively (active single-channel, passive, full-band) and can fall
    back to the base interface with **--use-base** (accepts a short uplink
    drop).
* Previous host attempt: link query on the client returned "Not connected."
  and an iw scan returned **-EBUSY** (command failed: Device or resource busy
  (-16)) - exactly the stale-iface/supplicant class the reset phase removes.
* The AP's .mgmt_tx is now live (g0 id6 carrier) and its .mgmt RX is live
  (g1 id13), so the client scan should expect **probe responses** as well as
  beacons; the harness uses an active scan FIRST (beacon-only passive scan was
  a workaround from the fail-closed period).

## Hard rules kept by this lane

* No router contact from the lane; no module load, no device write, no fire.
* No vendor binaries/firmware/ROM/calibration data in tracked files; the .ko
  and all live logs stay local (gitignored build/ tree or lane local-evidence/).
* No secrets: only the published lab PSK (test-passphrase-121) and the
  CHANGEME-lab-only placeholder appear; the scanner in the harness and the
  certification test both enforce this.
* The host harness never touches the base uplink unless **--use-base** is
  passed explicitly; cleanup restores NetworkManager in that case.

## Status (2026-11-25, task-158 handoff)

* Host harness: implemented + host-tested (23/23 tests, 41/41 self-checks,
  live read-only preflight PASS on the workstation).
* Router script: implemented + shell-checked + full dry-run checked; the
  bench stages are NOT run by this lane.
* Corrected module identity/provenance: verified PASS against the local
  artifact (/tmp/sibling-mgmt-fix, 344,372 B, sha ab9e18a4, 56 params, patch
  reproduces the 5 patched files).
* The association chain itself is the **next bench result to capture**:
  blocked only on the antennas + a clean client attempt. Fill MATRIX.md from
  the live evidence; the classification vocabulary and expected strings are
  already in it.
