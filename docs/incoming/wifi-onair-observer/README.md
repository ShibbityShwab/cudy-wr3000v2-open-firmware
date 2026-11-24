# wifi-onair-observer - host-side external-observer harness

**Lane:** wifi-onair-observer (shared task task-142), Cudy WR3000 v2.0
clean-room Hi5622 Wi-Fi RE project.
**Status: HOST-ONLY DESIGN + HOST-VERIFIED HARNESS - NOT RUN LIVE.**  Every
script, parser and PASS/FAIL rule was built and tested on the host; the
attended over-the-air run has **not** happened from this lane.  The Lead owns
the bench, the console and the recovery door and executes the runbook; the
harness never contacts the router.

**What this lane delivers:** a second managed Wi-Fi interface on the observer
host (default `wlan1`, created without touching the host's own uplink
`wlan0`) that passively scans for the bench AP `cudy-ap-test` on 2.4 GHz
channel 6, optionally captures raw beacons on a monitor interface, attempts
WPA2-PSK association with the published lab fixture and records the exact
4-way-handshake result, probes DHCP/ping when (and only if) association
completes, and runs a WPA3-SAE expected-negative variant.  Everything is
classified against the documented bench expectation, so the result is a
PASS/FAIL matrix, not a log dump.

## Why this lane exists

The 2026-11-29 one-boot validation proved, on the final all-patch module
(`opensource` 008aca5, stripped 308,080 B sha 5b587949): the five-message
vendor AP start -> hostapd **AP-ENABLED**, WPA2-AP GTK install (key_add
0x0078 + set_default_key 0x0079), TX COMPLETED, RX ARM PASSED.  What it could
not prove is anything **on the air**: host-side `AP-ENABLED` is not a
beacon, and every earlier on-air gate was blocked on "needs an external
observer".  This lane is that observer.

The expected bench outcome is part of the deliverable, not a surprise:

| rung | expected today | meaning |
| --- | --- | --- |
| L2 passive scan ch6 | **PASS** | decisive on-air beacon proof: SSID/BSSID/freq/DS channel/signal/beacon IEs |
| L3 WPA2 association | **EXPECTED-FAIL at AUTH** | the AP driver's `.mgmt_tx` is fail-closed and there is no management RX; a client AUTH gets no answer (the documented usable-AP blocker - the vendor does AP mgmt in host software via `wal_cfg80211_mgmt_tx`). This harness reproduces and quantifies it. |
| L4 DHCP / ping | **BLOCKED** unless L3 unexpectedly completes | no association, no L3; and the hostapd-only AP likely has no bridged VAP/dnsmasq |
| L5 WPA3-SAE | **SKIP / EXPECTED-FAIL** | SAE needs the same AUTH frame TX/RX path; the AP must first be restarted with the WPA2+WPA3 transition fixture |
| if L3 *does* complete | **UNEXPECTED-PASS** | then the 4-way handshake result is the decisive key-path datum; record it as new evidence |

A **PASS on L2 while L3 stops at AUTH** is the expected, useful outcome: it
separates "beacon engine works" from "usable AP needs the mgmt path", which is
exactly the open question left by the static-RE wave.

## Deliverables

| path | what it is |
| --- | --- |
| [RUNBOOK.md](RUNBOOK.md) | the attended procedure: preconditions, L0-L5 commands, expected outputs, failure diagnosis, cleanup, stop rules, PASS/FAIL matrix |
| [RESULTS-TEMPLATE.md](RESULTS-TEMPLATE.md) | fill-in template for a live run (copy into `results/` after the run) |
| [tools/run-all.sh](tools/run-all.sh) | one command: preflight -> iface -> scan -> (capture) -> WPA2 assoc -> DHCP/ping -> cleanup, writes `verdicts.txt` + `matrix.md` |
| [tools/preflight.sh](tools/preflight.sh) | L0 host readiness + base-uplink guard snapshot (`--check-only` is safe, read-only) |
| [tools/iface-up.sh](tools/iface-up.sh) | L1 create/delete the second managed interface (and `--monitor` for the capture rung) |
| [tools/scan-ap.sh](tools/scan-ap.sh) + [tools/onair_scan.py](tools/onair_scan.py) | L2 passive ch6 scan and the beacon verdict parser |
| [tools/wpa2-assoc.sh](tools/wpa2-assoc.sh) + [tools/wpa_assess.py](tools/wpa_assess.py) | L3 dedicated wpa_supplicant attempt; stage/four-way classification |
| [tools/wpa3-sae.sh](tools/wpa3-sae.sh) | L5 WPA3-SAE expected-negative variant (fail-closed precondition: beacon must advertise SAE) |
| [tools/dhcp-test.sh](tools/dhcp-test.sh) + [tools/dhcp_probe.py](tools/dhcp_probe.py) | L4 DHCP/ping (bundled minimal DHCP client - the host has none) |
| [tools/capture-beacon.sh](tools/capture-beacon.sh) + [tools/sniff.py](tools/sniff.py) | L2b optional monitor-mode beacon/auth capture (bundled raw-socket sniffer - the host has no tcpdump/tshark) |
| [tools/cleanup.sh](tools/cleanup.sh) | idempotent host restore + final base-uplink guard |
| [configs/](configs/) | host-side wpa_supplicant fixtures (WPA2-PSK, WPA3-SAE); lab values only |
| [tests/selftest.py](tests/selftest.py) + [tests/mockcmd.py](tests/mockcmd.py) | host-only test suite: parsers, classifiers, DHCP/sniffer units, and a fully mocked end-to-end dry run |
| [evidence/](evidence/) | committed host evidence (selftest, real read-only preflight of the observer host) |

## Quick start (attended, by the Lead)

    cd opensource/docs/incoming/wifi-onair-observer

    # host-only self test (no radio, no root, no device)
    python3 tests/selftest.py

    # read-only host readiness report (safe)
    tools/preflight.sh --check-only

    # the attended run (bench AP already AP-ENABLED in the Lead's console)
    sudo tools/run-all.sh --label ap-final --with-monitor

    # if the run is interrupted, always restore the host:
    sudo tools/cleanup.sh

On the build workstation the observer host has a single MT7922 combo radio
(`wiphy0`) and `wlan0` is the protected workstation uplink; the observer is
a **second virtual managed interface on the same wiphy** (`iw phy phy0
interface add wlan1 type managed`), which that wiphy advertises
(`#{ managed } <= 2`, `#channels <= 2`).  A host with a second physical
adapter can point the harness at it with `ONAIR_IFACE`/`ONAIR_BASE_IF`.
The harness snapshots `wlan0` before RF work and re-checks it after every
phase; an unexpected drop is a hard failure (`GUARD-FAIL`) that aborts the
run and triggers cleanup.  No host command ever addresses `wlan0` except
read-only `iw dev wlan0 info/link`.

## Evidence and security boundaries

* Live evidence goes to the parent notebook's gitignored
  `build/wifi-onair-observer/<UTC>/<label>/` (or `local-evidence/` in the
  curated repo, also gitignored).  Committed evidence is text-only host
  self-test results.
* Monitor pcaps contain ordinary over-the-air traffic from **all** nearby
  networks.  They stay local, are never committed, and the harness summary
  never writes neighbour SSIDs/BSSIDs - only counts.  Scrub before sharing.
* The WPA2 passphrase `test-passphrase-121` is the published bench test value
  already used by the AP-side fixture [wifi-ap-start/recipes/hostapd-wpa2.conf](../wifi-ap-start/recipes/hostapd-wpa2.conf);
  it is not a secret and not a credential for any real network.  No stock
  password, per-unit key, `bdinfo` secret or key material belongs in this lane.
* No vendor binaries, firmware/ROM/calibration bytes or decrypted vendor files
  are referenced or committed here.

## Rules honoured

Host-only: no 192.168.1.1 / 192.168.10.1, no UART, no TFTP, no flash, no
devmem, no module load/unload, no reboot - the harness contains **no
router-access command at all** (the selftest greps for that).  The bench run is
attended by the Lead with the recovery door armed, per the project rules.  The
harness is fail-closed: it refuses interface names other than `wlanN`/`mon*`,
refuses the bench USB-Ethernet link `enp195s0f3u1`, never mutates the base
uplink, and its dry-run mode refuses to execute any non-mock binary.

## Related evidence in this tree

* [wifi-beacon-static/LADDER-BEACON.md](../wifi-beacon-static/LADDER-BEACON.md) -
  the ladder whose "external observer on-air gate" this lane satisfies.
* [wifi-beacon-static/REPORT.md](../wifi-beacon-static/REPORT.md) - vendor AP
  mgmt-in-host-software finding (`wal_cfg80211_mgmt_tx`), the expected L3
  blocker.
* [wifi-re-report/REPORT.md](../wifi-re-report/REPORT.md) - F15-F20 / C46-C56:
  RX live-bank + `dp_rx_arm`, WPA2 key protocol + GTK fire, five-message AP
  order, STA/5 GHz/HT tables, final all-patch one-boot validation.
* [wifi-wpa-static/LADDER.md](../wifi-wpa-static/LADDER.md) and
  [TABLES.md](../wifi-wpa-static/TABLES.md) - key command decode (0x0078/79/7A).
* [wifi-sta-static/TEST-LADDER.md](../wifi-sta-static/TEST-LADDER.md) - STA
  join protocol (the opposite direction) and its expected-negative ladders.
* Parent notebook `STATUS.md` section "Next action" item 1 - "AP breadth with
  an external observer"; and the 2026-11-29 ledger entries for the final
  module identity.

## Reproduce the host-only proof

    cd opensource/docs/incoming/wifi-onair-observer
    python3 tests/selftest.py          # expect: selftest: 216/216 passed, 0 failed

The suite runs: shell/python syntax, document/config checks, the iw-scan
parser against four beacon fixtures, the association classifier against five
supplicant captures, DHCP packet build/parse units, radiotap/beacon/auth
sniffer units + pcap round-trip, static safety checks, and two mocked
`run-all.sh --dry-run` scenarios: the normal one must produce the expected
`scan=PASS / wpa2-assoc=EXPECTED-FAIL / dhcp=SKIP / cleanup=PASS` matrix, and
a negative control (no beacon in the scan) must gate the association attempt
(`scan=FAIL / wpa2-assoc=SKIP`) - all with zero host mutation.