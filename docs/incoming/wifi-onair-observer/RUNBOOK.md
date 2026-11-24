# RUNBOOK - external-observer on-air test for `cudy-ap-test` (attended, host-only harness)

**Status: design + host-tested harness.  NOT RUN LIVE YET.**  This runbook is
executed by the Lead, at the bench, with the recovery door armed and the bench
AP already up in the console session.  The harness itself is host-only and
contains no router-access command; see §0.

## 0. Scope and hard boundaries

* **No bench-router contact.**  Nothing in this lane runs SSH, UART, TFTP,
  flash, devmem, module load/unload or reboot against the router.  The Lead
  drives the router from the existing console; this harness only transmits and
  receives ordinary 802.11 frames over the air as a Wi-Fi client.
* **The host uplink `wlan0` is protected.**  All phases snapshot it with
  `iw dev wlan0 info/link` and re-check it afterwards.  If it loses its
  connection, the phase records `GUARD-FAIL` and the run aborts + cleans up.
  Never run a command against `wlan0` by hand during the run.
* **The bench USB-Ethernet link `enp195s0f3u1` is never addressed.**
  `lib.sh` refuses it by name; the selftest checks for it.
* **Lab values only.**  The fixture passphrase `test-passphrase-121` is the
  published bench test value (same as
  [../wifi-ap-start/recipes/hostapd-wpa2.conf](../wifi-ap-start/recipes/hostapd-wpa2.conf)).
  Never substitute a real password/key here.  `ONAIR_PSK` may override the
  value at runtime; it is never logged and the evidence copy is redacted.
* **One radio, two interfaces.**  On the build workstation the observer host
  has a single MT7922 wiphy;
  `wlan1` is a second virtual managed interface on that wiphy.  The host
  uplink may go briefly off-channel during scans; that is expected and the
  guard only fails if it actually disconnects.
* **Pcap privacy.**  `beacons.pcap` contains frames from all nearby
  networks.  It stays in the local evidence directory (gitignored, never
  committed) and is deleted or archived by the operator after analysis.

## 1. Preconditions (all mandatory)

### 1.1 Bench side (Lead, before the observer run)

| check | requirement | evidence |
| --- | --- | --- |
| AP running | final all-patch module loaded and hostapd AP-ENABLED on the router: `ssid cudy-ap-test`, `type AP`, 2.4 GHz channel 6, WPA2 fixture | console: `iw dev wlan0 info`; dmesg AP-start/GTK lines per the 2026-11-29 ledger entry |
| AP config | hostapd started with the WPA2 fixture (`hostapd-wpa2.conf`: wpa=2, WPA-PSK, CCMP, channel 6, beacon_int 100, dtim 2) - no HT block unless the fixture used has `ieee80211n=1` | console; record which fixture |
| bench link / recovery | bench link PASS; recovery door READY; UART capture running (project rules) | Lead's own pre-fire checks |
| console capture | while the observer runs, keep the console/dmesg capture running; the client's AUTH frame may enter the MAC RX path (the "RX capture with real traffic" rung of the parent plan) | UART log path |
| stop triggers known | page 0x86807FEF golden / mask 0 / no exceptions - and the stop rules in the parent RUNBOOK still apply | Lead |

The AP does **not** need to be restarted for L0-L4 (WPA2 fixture).  For L5 the
AP must be restarted with the WPA2+WPA3 transition fixture - see §5.

**Dependency note:** a parallel host-only lane (shared task task-143,
`wifi-ap-mgmt`) targets the usable-AP blocker (the WAL/cfg80211 mgmt-tx
family + D2H mgmt RX decode).  Until that lands, L3 is expected to stop at
AUTH; once it is fired and validated, re-run this harness - a completed
association then becomes the expected result and the 4-way handshake datum is
the deliverable.  The PASS/FAIL matrix already treats a completed association
as UNEXPECTED-PASS (not a regression).

### 1.2 Host side (observer workstation)

| check | requirement | how the harness checks |
| --- | --- | --- |
| root | all RF/netdev phases need root; `run-all.sh` is run with `sudo` | `preflight` |
| tools | `iw`, `wpa_supplicant`, `wpa_cli`, `ip`, `nmcli`, `python3` | `preflight` |
| wiphy | managed mode, 2 managed interfaces per combination, 2 channels; monitor advertised for L2b | `preflight` reads `iw phy` |
| host uplink | `wlan0` exists (connected or not); record its SSID/channel in the guard snapshot | `preflight` |
| observer name | `wlan1` free; no stale `wlan1`/`mon-onair` | `preflight`, `cleanup` |
| passphrase | fixture value or `ONAIR_PSK` if the bench AP uses another lab passphrase | fixtures |
| host Wi-Fi free | no other lane/process using the observer host's Wi-Fi: no leftover monitor iface (`mon0`), no concurrent STA/hotspot connection test, `wlan0` present as a managed interface | `ip -br link`, `iw dev`; preflight FAILs (by design) if the base iface is absent |

Read-only check (safe any time, no root):

    cd opensource/docs/incoming/wifi-onair-observer
    tools/preflight.sh --check-only

If the host has a second physical Wi-Fi adapter instead of the virtual second
interface, set `ONAIR_IFACE` to the other adapter name (`wlanN`) and
`ONAIR_BASE_IF` to the protected uplink (default `wlan0`).  The
harness never assumes the two ifaces share a wiphy.

## 2. Run overview and expected result classes

    sudo tools/run-all.sh --label ap-final [--with-monitor] [--full] [--wpa3]

Phases and expected classes **today** (documented expectation, from the
static-RE wave; not a promise - anything different is recorded and is new
evidence):

| # | phase | command (orchestrated) | expected result |
| --- | --- | --- | --- |
| L0 | preflight | `tools/preflight.sh` | **PASS** |
| L1 | second managed iface | `tools/iface-up.sh` | **PASS** (may FAIL with `-EBUSY` if the host wiphy refuses; that is a host limitation, not a router defect) |
| L2 | passive on-air beacon scan ch6 | `tools/scan-ap.sh` | **PASS** - decisive on-air beacon proof (SSID, BSSID, freq 2437, DS ch 6, signal, beacon IEs incl. RSN for the WPA2 fixture) |
| L2b | optional monitor beacon capture | `tools/capture-beacon.sh` | **PASS**, or **SKIP** if the wiphy cannot run monitor + managed together |
| L3 | WPA2-PSK association | `tools/wpa2-assoc.sh` | **EXPECTED-FAIL at AUTH** (AP `.mgmt_tx` fail-closed, no mgmt RX) |
| L4 | DHCP + ping | `tools/dhcp-test.sh` | **SKIP** when L3 is not complete (expected); **BLOCKED** if association works but no DHCP server answers |
| L5 | WPA3-SAE variant | `tools/wpa3-sae.sh` | **SKIP** unless the AP was restarted with the transition fixture; then **EXPECTED-FAIL at SAE/AUTH** |
| - | cleanup | `tools/cleanup.sh` | **PASS** (always) |

The run writes, in the evidence directory:

    verdicts.txt      one ONAIR-VERDICT|phase|status|detail line per phase
    matrix.md         the PASS/FAIL matrix with expectation column + findings
    <phase>.log       full command/output log per phase
    scan-ch6.txt/.json, supplicant.log, assoc-result.json, dhcp-result.json, ...

Exit code: **0** = run completed and every phase matched its expected class;
**1** = completed with unexpected findings (inspect `matrix.md`); **2** =
harness/safety error (guard/cleanup - restore the host with `cleanup.sh`).

## 3. Phase detail, commands and expected output shapes

All examples below are the **expected output shape**; the exact numbers depend
on the bench.  (The committed `tests/fixtures/` show the same shapes; they
are synthetic test data, not hardware results.)

### L0 - preflight

    sudo tools/preflight.sh

Expected highlights:

    [preflight] ok   iw: iw version 6.17
    [preflight] ok   wpa_supplicant: wpa_supplicant v2.12
    [preflight] ok   base iface wlan0 present
    [preflight] guard: base uplink wlan0 is connected before RF work ...
    [preflight] ok   managed mode supported
    [preflight] ok   wiphy allows 2 managed interfaces ...
    [preflight] ok   wiphy supports 2 channels concurrently ...
    [preflight] ok   observer iface wlan1 does not exist ...
    [preflight] ok   root: live phases may run
    ONAIR-VERDICT|preflight|PASS|...

PASS here means the host can be a clean observer; FAIL lists the missing tool
or capability and the run should not proceed.

### L1 - create the second managed interface

    sudo tools/iface-up.sh

Expected: `creating wlan1 on phy0 (base iface wlan0 stays untouched)`,
`observer iface MAC: 02:...` (a locally-administered MAC, not the host
uplink MAC), `guard: base uplink wlan0 still connected`,
`ONAIR-VERDICT|iface-up|PASS`.

The script first installs
`/etc/NetworkManager/conf.d/99-wifi-onair-observer.conf` with
`unmanaged-devices=interface-name:wlan1;interface-name:mon-onair` so NM
cannot grab the observer interface, then does `nmcli device set wlan1
managed no` at runtime as a second layer.  Neither touches NM's management of
`wlan0`.

If the wiphy refuses the second interface (`-EBUSY`, `Operation not
supported`), record L1 FAIL and stop: this host cannot run the observer
harness in this configuration (a second physical adapter would be required).

### L2 - the decisive rung: passive scan for the beacon

    sudo tools/scan-ap.sh

The default is a **passive** scan on 2437 MHz with a 1500 TU dwell (no probe
requests).  This is deliberate: the clean-room AP's mgmt-TX path is fail-closed,
so probe responses may never be sent; the beacon (firmware-generated on the
vendor path) is what we want to hear.  Expected output (shape):

    [scan] mode: passive on one frequency (no probe requests; only beacons can answer)
    scan: blocks=N other-aps=M target=yes
      [ok] ssid_found               1 BSS match the expected SSID
      [ok] bssid_valid              02:00:00:... 
      [ok] ie_source_beacon         IE source: beacon
      [ok] freq_expected            freq=2437 (expected 2437)
      [ok] ds_channel_expected      DS channel=6 (expected 6)
      [ok] beacon_interval_100tu    beacon interval=100 TU
      [ok] capability_privacy       capability=0x0411
      [ok] rsn_ie_present           RSN IE present: True
      [ok] rsn_psk_akm              AKM suites: PSK
      [ok] rsn_ccmp                 pairwise ciphers: CCMP
    scan: PASS: beacon on air: ssid='cudy-ap-test' bssid=... freq=2437 ch=6 ...
    ONAIR-VERDICT|scan|PASS|on-air beacon proof on ch6 ...

PASS is the **decisive on-air beacon proof** this whole line was missing.
Cross-check on the router console: the beacon BSSID should be the AP VAP MAC
(`iw dev wlan0 info` on the router); the DS channel must be 6.

If the SSID is not found, the script automatically runs a full-band passive
scan as diagnosis and records where (if anywhere) the SSID appears.  A FAIL
here means either the beacon is not on the air (hand off to the beacon path:
firmware SMAC/TBTT/VAP state - the static candidate was already fixed, so a
clean L2 host start with no beacon is a new firmware-side finding) or the AP
is on a different channel than configured (the fallback output says which).

### L3 - WPA2-PSK association attempt

    sudo tools/wpa2-assoc.sh

The harness stages the fixture (passphrase redacted in the evidence copy),
starts a **dedicated** wpa_supplicant instance on `wlan1` with its own
control socket (`/run/wpa_supplicant-onair`, never NM's), polls
`wpa_cli status` up to `ONAIR_ASSOC_TIMEOUT` (default 45 s), always stops
the instance, then classifies the capture.

**Expected outcome today - AUTH-TIMEOUT:**

    wlan1: SME: Trying to authenticate with <bssid> (SSID='cudy-ap-test' freq=2437 MHz)
    wlan1: Trying to associate with <bssid> (SSID='cudy-ap-test' freq=2437 MHz)
    wlan1: Authentication with <bssid> timed out.
    ...
    assoc stage: AUTH-TIMEOUT
    assoc complete: False   four-way: not-reached
    expectation: EXPECTED-FAIL
    verdict: EXPECTED-FAIL: attempt stopped at AUTH-TIMEOUT - EXPECTED: client AUTH frame sent,
      no AP management response (...mgmt-TX fail-closed...). This reproduces the documented
      usable-AP blocker, it is not a harness defect.
    ONAIR-VERDICT|wpa2-assoc|EXPECTED-FAIL|...

This is the useful negative: it quantifies the usable-AP blocker on the wire.
With `--with-monitor` the pcap can additionally show the client's AUTH frame
on air with **no AP AUTH reply** (the sniffer reports
`auth_to_ap>0, auth_from_ap==0`).

If instead the attempt reaches `ASSOC-TIMEOUT`/`ASSOC-REJECT`, AUTH was
answered somehow - record it as new evidence (partial mgmt path).
If it reaches **`COMPLETED`** (UNEXPECTED-PASS): the 4-way handshake result
is captured and decisive:
* `WPA: Key negotiation completed ... [PTK=CCMP GTK=CCMP]` ->
  **4-way PASS** - WPA2 association is real on air; the AP-side key path
  (GTK was proven installed at AP start) completed for a client.
* `4-Way Handshake failed` -> **FAIL-4WAY** - association worked, key
  install/4-way path defect; capture the AP-side key logs in the console.

### L4 - DHCP and reachability (only if L3 completed)

    sudo tools/dhcp-test.sh

Not reached in the expected run (L3 expected-fail -> `dhcp SKIP`).  If L3
completes, the harness runs a real DHCPDISCOVER/REQUEST using the bundled
`tools/dhcp_probe.py` (the host has no dhclient/dhcpcd/udhcpc), applies the
lease, and pings the DHCP router (or `ONAIR_PING_TARGET`).

No reply is classified **BLOCKED**, not a harness failure: the AP is hostapd
started by hand and `wlan0` may not be bridged into the router's `br-lan`
or served by dnsmasq.  The run log prints the router-side checks the Lead can
do in the console (bridge link/dnsmasq - the harness never contacts the
router).  A lease that then fails ping is a **data-path finding** (FAIL).

Manual variants:

    sudo tools/dhcp-test.sh --static 192.168.10.50/24 --ping-target 192.168.10.1
    sudo tools/dhcp-test.sh --no-ping

### L2b (optional) - monitor-mode beacon capture

    sudo tools/iface-up.sh --monitor
    sudo tools/capture-beacon.sh --duration 20

Because the host has no tcpdump/tshark, the lane ships a minimal raw-socket
sniffer (`tools/sniff.py`).  It writes `beacons.pcap` (radiotap, openable
in Wireshark) and a JSON summary: beacon count, median interval (expect
~102 ms for `beacon_int=100`), RSSI, IE list, and AUTH-frame counters.
The capture is driven while a passive scan parks the phy on the AP channel; it
is **best effort**: if the host wiphy cannot run monitor + managed together
the rung is SKIP, and the L2 scan remains the decisive proof.

The equal-or-better alternative on this bench: run the sniffer manually while
the Lead triggers the association attempt on `wlan1` (the harness's L3), to
record the AUTH frame with no AP reply:

    sudo python3 tools/sniff.py --iface mon-onair --ssid cudy-ap-test \
        --duration 30 --pcap-out /tmp/onair-auth.pcap --json-out /tmp/onair-auth.json

## 5. WPA3-SAE variant (L5) - expected-negative

    sudo tools/wpa3-sae.sh

**Precondition:** the AP must have been restarted by the Lead with the
WPA2+WPA3 transition fixture
([../wifi-image-polish/rootfs/etc/hi5622/hostapd-wpa2-wpa3.conf](../wifi-image-polish/rootfs/etc/hi5622/hostapd-wpa2-wpa3.conf):
`wpa=2`, `wpa_key_mgmt=WPA-PSK SAE`, `ieee80211w=1`, SAE-capable
hostapd-mini), and L2 re-run so the beacon shows the SAE AKM.  The harness is
fail-closed: if the beacon does not advertise SAE it records **SKIP** with
"AP still broadcasting WPA2-only" rather than a misleading WPA3 failure.

Expected today: **EXPECTED-FAIL** - SAE needs management AUTH frame TX/RX; the
clean-room AP `.mgmt_tx` is fail-closed and there is no mgmt RX.  Do **not**
report WPA3 as working from this run even if the client logs are ambiguous;
the classifier records the exact stage (`SAE: ...` lines + auth timeout).

## 6. PASS/FAIL matrix (fill after the run)

| phase | expected today | actual | evidence | verdict |
| --- | --- | --- | --- | --- |
| L0 preflight | PASS | | preflight.log, guard-wlan0.txt | |
| L1 second iface | PASS | | iface-up.log, observer-iface-info.txt | |
| L2 passive ch6 beacon | **PASS (decisive)** | | scan-ch6.txt/.json | |
| L2b monitor capture (opt) | PASS or SKIP | | beacon-summary.json, beacons.pcap (local) | |
| L3 WPA2 association | EXPECTED-FAIL at AUTH | | supplicant.log, status-final.txt, assoc-result.json | |
| L3b 4-way handshake | not reached (expected) | | handshake-evidence.txt (if reached) | |
| L4 DHCP | BLOCKED/SKIP (expected) | | dhcp-result.json | |
| L4b ping | SKIP (expected) | | ping.txt | |
| L5 WPA3-SAE | SKIP or EXPECTED-FAIL | | wpa3-sae.log, scan-sae-check.json | |
| cleanup | PASS | | cleanup.log, guard-wlan0.txt | |

Overall grading:
* **ON-AIR BEACON PASS** = L2 PASS.  This is the lane's primary deliverable and
  the "external observer" gate of the parent plan.
* **HARNESS PASS** = every phase matched its expected class (exit 0).
* Also record surprise outcomes prominently: a completed association / 4-way
  handshake / DHCP lease is **good news**, not a failure; it retires the
  usable-AP blocker claim and must be written into the RE report and ledger.

## 7. Failure diagnosis

| symptom | likely cause | action |
| --- | --- | --- |
| L1: `iw phy ... interface add` fails (-EBUSY/-EOPNOTSUPP) | wiphy combination / NM grabbed the iface / stale iface | run `sudo tools/cleanup.sh`; check `iw phy` combinations; a second physical adapter may be required - record host limitation |
| L2: scan result has other APs but not `cudy-ap-test` | beacon not on air; wrong channel; AP not started/AP-ENABLED only; host too far | keep the fallback full-band scan output; verify on the router console that hostapd is AP-ENABLED on ch6 and the page/mask are golden; check the beacon path (firmware SMAC/TBTT/VAP), not the host fixture |
| L2: SSID found on a different frequency | cfg80211/hostapd channel mismatch | the fallback JSON names the freq; fix the AP-side hostapd channel or the module channel context, then re-run |
| L2: SSID found but no RSN / no privacy | AP started with the open fixture | re-run with `tools/scan-ap.sh --expect-open` or restart the AP with the WPA2 fixture |
| L2: scan errors (-EBUSY / "Operation not supported") | another wpa_supplicant attached to `wlan1`; NM managing it | `sudo tools/cleanup.sh`; ensure the NM drop-in installed (iface-up does it) |
| L3: wpa_supplicant did not start / ctrl socket missing | non-root; stale instance; iface down | run with sudo; `cleanup.sh`; check `iw dev wlan1 info` |
| L3: AUTH-TIMEOUT | expected (AP mgmt path fail-closed) | record as EXPECTED-FAIL; the monitor pcap shows the unanswered AUTH |
| L3: ASSOC-TIMEOUT / completed | AUTH was answered / AP mgmt path partially working | unexpected: record exact stage, keep the pcap and supplicant log; the 4-way result is the key-path datum |
| L3: 4-WAY failed | key install/4-way defect while association worked | keep the supplicant log; capture AP-side key_add/set_default_key dmesg in the console |
| L4: no DHCP reply (BLOCKED) | `wlan0` not in `br-lan` / no dnsmasq on the VAP | Lead checks the console hints; or use `--static` + `--ping-target`; DHCP is not required for the on-air beacon verdict |
| L2b: no frames captured | monitor cannot coexist on the wiphy; monitor on another channel; wrong iface up | verify `iw dev mon-onair info`; if SKIP, rely on L2; never fake a beacon capture |
| any phase: GUARD-FAIL | host uplink `wlan0` disconnected | run aborts; `tools/cleanup.sh`; reconnect the host uplink and re-run; this is a host problem, not a router finding |
| `wpa_cli: Permission denied` | not root / wrong ctrl dir | run phases with sudo; the harness's ctrl dir is `/run/wpa_supplicant-onair` |

## 8. Cleanup and restore (always)

    sudo tools/cleanup.sh            # default
    sudo tools/cleanup.sh --iface-only   # keep the NM drop-in (debugging only)

Cleanup stops only the harness's wpa_supplicant (never NM's), kills a leftover
sniffer, deletes `wlan1` and `mon-onair`, removes the NM drop-in it
installed, removes the temporary supplicant config/pid, and re-checks
`wlan0`.  It is idempotent; `run-all.sh` calls it automatically even on
abort.  Verify afterwards:

    iw dev                       # must not list wlan1 / mon-onair
    nmcli device status          # wlan0 back to its normal state
    ping -c1 <host uplink peer>  # host internet still works

If anything is left over, the last-resort manual cleanup is the same commands
`cleanup.sh` runs; never delete `wlan0`.

## 9. Evidence and result record

1. Select the evidence dir printed by `run-all.sh` (default
   `<parent-repo>/build/wifi-onair-observer/<UTC>-<label>/`; gitignored).
2. Copy [RESULTS-TEMPLATE.md](RESULTS-TEMPLATE.md) to
   `results/onair-<date>-<label>.md` and fill it from `matrix.md` + the
   logs.  Committed text must not contain neighbour SSIDs/BSSIDs, pcaps, or
   any credential; redact as needed.
3. Router-side companion notes (Lead): the console/dmesg lines observed while
   the observer traffic ran (AP-start/GTK lines, `dp_rx_drain`/RX
   completions, firmware event ids, page/mask readbacks).  These are console
   reads, not harness commands; record them in the results file.
4. If L3/L4 surprises: also update the RE report/ledger with the on-air
   findings.

## 10. Stop rules

Abort immediately (Ctrl-C / `cleanup.sh`) and record, if:
* the base uplink `wlan0` loses its connection or the guard reports
  GUARD-FAIL;
* any host command hangs more than its timeout;
* `wlan1` creation/deletion leaves a stale interface;
* the Lead's console shows a stop-rule trigger on the router (page/mask/
  exception/staging per the parent RUNBOOK) - the Lead aborts the bench, the
  observer follows with cleanup;
* the monitor capture / scan starts interfering with the host uplink.

Never continue to a later phase after a harness error (rc 2).  Never re-run an
association attempt without cleanup in between.

## 11. Host-only reproduction (no radio, no device)

    cd opensource/docs/incoming/wifi-onair-observer
    python3 tests/selftest.py
    # expected: selftest: 216/216 passed, 0 failed
    ./tools/preflight.sh --check-only     # real host report, read-only

The selftest includes a fully mocked `tools/run-all.sh --dry-run` proving
the orchestration/verdict/matrix path without touching the host network
(`tests/mockcmd.py` simulates iw/ip/nmcli/wpa_supplicant/wpa_cli/id/ping).