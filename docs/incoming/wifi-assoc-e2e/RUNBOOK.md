# RUNBOOK - client association E2E (attended bench fire)

**Status:** design + host-tested; NOT fired by the lane. The Lead executes this
at the bench with the recovery door armed. HOST-ONLY lane: no command here is
run by the lane against the router.

Normal font style: **PASS** means the rung's gate was met, **STOP** means end
the boot (no in-load retry). The one-line outcome of every rung goes into
[MATRIX.md](MATRIX.md); symptom -> cause lives in [DIAGNOSIS.md](DIAGNOSIS.md).

---

## 0. Scope, safety and preconditions

The goal is the first real client association + 4-way handshake to the
clean-room AP: client auth/assoc on air -> hostapd 5-message AP already up ->
mgmt TX (g0 id6) -> mgmt RX (g1 id13) -> .add_station (g0 id7 + 0x07d2) ->
EAPOL msg1/msg3 via ndo_start_xmit (data_tx) -> EAPOL msg2/msg4 via the MAC RX
ring (dp_rx_drain) -> PTK/GTK keys -> client COMPLETED.

Preconditions (all mandatory; the Lead owns them, unchanged project rules):

| # | precondition | why |
| --- | --- | --- |
| P1 | recovery door READY; a human at the bench; UART capture running to an absolute path | physical recovery is the only recovery |
| P2 | fresh golden boot: page 0x86807fef/7/2/0xabcd, state 1, mask 0, staging 0 | every Wi-Fi fire starts from this |
| P3 | PD4 firmware pushed by hand (/lib/firmware/hi_wifi/FIRMWARE.PD4.bin, sha cfe24bfb...) and the local Wi-Fi block staged; never packaged/committed | firmware policy |
| P4 | final image #2 (corrected association module + WPA2/WPA3 fixtures + SAE hostapd); if the image predates it, deploy the verified .ko + fixtures to /root and /etc/hi5622 as in section 1 | the corrected carrier is not in older images |
| P5 | host workstation: MT7922, iw 6.17, wpa_supplicant 2.12, nmcli, python3; host uplink not needed during --use-base rungs | client harness prerequisites |
| P6 | antennas ATTACHED and roughly aligned (RF gate C2 below) | without them the client cannot be heard (-63 dBm was the no-antenna beacon level) |

The station-ops / data-tx / mgmt layers are all fail-closed by default; the
association run enables exactly the 11 feature params. Never change more than
one rung per boot; one load per boot.

---

## 1. Artifacts and host-side verification (before the router is touched)

Host:

    cd opensource/docs/incoming/wifi-assoc-e2e
    ./scripts/verify-association-module.sh            # RESULT: PASS required
    python3 tests/test_assoc_e2e.py                   # 23 tests PASS
    python3 scripts/assoc_e2e.py verify               # 41/41 PASS

Expected module identity (if any line differs: STOP, do not load it):

| check | expected |
| --- | --- |
| size | 344,372 B |
| sha256 | ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e |
| params | 56 |
| carrier | g0 id6 builder present; old "WAL cfg-event carrier" string ABSENT |
| provenance | association tree + sibling patch = the 5 patched files byte-identical |

If /tmp was cleaned: ../wifi-stability/tools/build-association-module.sh then
apply ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch, rebuild,
strip, re-verify (the verifier prints the full recipe with --rebuild).

Copy to the router (console or tools/rscp.sh; the image should already carry
/etc/hi5622/hostapd-open.proven.conf, hostapd-wpa2.conf and
hostapd-wpa3-sae.conf - if not, copy them too):

    /root/hi5622_assoc.ko          (the verified 344,372 B artifact)
    /root/assoc-router-fire.sh     (scripts/router/assoc-router-fire.sh)

On the router:

    sha256sum /root/hi5622_assoc.ko     # must equal ab9e18a4...
    sh /root/assoc-router-fire.sh --check

--check is read-only: attrs present, module state, firmware file hashes,
golden snapshot. STOP if it fails (do not insmod on a non-golden page).

---

## 2. Router rungs (Lead console)

Why this exact order: the AP start re-initializes the MAC, so the RX bank is
only armed when the data plane comes AFTER the AP. With dp_setup before the AP
the live RX bank reads status 0x02 (cleared) instead of 0x182 (armed). This was
a hardware-observed ordering correction - do not reorder.

### R1 - load the corrected module + boot ladder + C2

    sh /root/assoc-router-fire.sh --load /root/hi5622_assoc.ko
    sh /root/assoc-router-fire.sh --boot
    sh /root/assoc-router-fire.sh --c2

PASS evidence (dmesg, in order):

* insmod rc=0; module params print mgmt_tx=1 mgmt_rx=1 ... (the script prints
  the 11 feature params).
* boot ladder: prepare/upload/verify/seed/hcc_arm/hcc_loop/release all rc=0;
  hcc_ready contains id1_seen=1 id2_seen=1; ready in a few seconds.
* C2: h2d_ctx_create and h2d_txq_sync72 consumed 0 polls (h2d_status ok=3
  after the txq sync).
* Golden after every rung: page 0x86807fef/7/2/0xabcd, state 1, mask 0,
  staging 0.

STOP: any announce not consumed, any exception, page/mask/staging deviation ->
physical power-cycle.

### R2 - AP start for THIS boot's profile (choose it now)

**One AP profile per boot** is the rule for this E2E. The AP start initializes
the MAC and the data-plane arming is one-shot per load, so a hostapd profile
restart after the DP chain would leave the RX bank unverified (a restart is
supported by the E2 driver, but it is not part of this association run). Pick
the profile for this boot and keep it:

* Boot A (control): open     - proves association + station ops without crypto
* Boot B (primary): wpa2     - the 4-way handshake target
* Boot C (optional): wpa3    - SAE + PMF, needs a CONFIG_SAE hostapd image

    sh /root/assoc-router-fire.sh --stage ap --profile open     # Boot A
    sh /root/assoc-router-fire.sh --stage ap --profile wpa2     # Boot B
    sh /root/assoc-router-fire.sh --stage ap --profile wpa3     # Boot C

PASS evidence:

* hostapd log: wlan0: AP-ENABLED
* dmesg vendor 5-message order (all posted, announce consumed):
  plan (5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start
  frame post vap_add ... / frame post ap_chan ... (32 B, band=0 ch=6)
  frame post ap_ssid ... / frame post ap_cfg ... / frame post ap_vap_start ...
  cfg80211: start_ap beacon_static: 'ap_vap_start' posted after ap_cfg
    (channel=6) - vendor AP-start order complete
* iw dev wlan0 info on the router: type AP, ssid wr3000v2-apstart
* golden snapshot.

### R3 - data plane in the corrected order + proof of RX armed

    sh /root/assoc-router-fire.sh --stage dp
    sh /root/assoc-router-fire.sh --stage drain-start

PASS evidence (dmesg):

* DP setup: OK - rings 256/512/4095, TXQ table 0x2524 devva 0x..., MAC band0
  14 regs written+verified (mask 0xfffffcc1)
* DP txq_sync: g0 id1 key 0x0320 sent with TXQ-table devVA ...
* DP rx_arm: live bank CA 0x40050000 status=0x00000182 rx_pending=0
  mask=0xfffffcc1 (vendor 0xfffffcc1) norm_rptr=0x0020 small_rptr=0x0020 ...
  + "free-rptr movement proves the MAC consumed published free entries"
* drain loop pid running (bounded 1 s cadence; logs "no new completion" until
  a client frame arrives).

STOP: status != 0x182 / mask != 0xfffffcc1 -> the AP-before-DP order was not
followed or the MAC was re-initialized; do not proceed. Exception -> power-cycle.

At the end of R3 the router is fully ready; everything else is client-side plus
evidence collection.

---

## 3. Host client phases (same boot; host is separate from the router)

All host phases need root because they change the NIC state. Evidence goes to
the repo's gitignored build/wifi-assoc-e2e/<UTC-stamp>/ by default.

### C0 - preflight (read-only, no root needed)

    python3 scripts/assoc_e2e.py preflight

PASS: tools present, wlan0 exists, the MT7922 wiphy advertises 2437 MHz,
2-managed combination, rfkill unblocked, lane-secret scan PASS.
If it reports the wiphy cannot do a second managed interface: use --use-base
for every subsequent command (and accept that the host uplink drops until
cleanup).

### C1 - reset (one supplicant, NM out of the way, EBUSY class removed)

    sudo python3 scripts/assoc_e2e.py reset

What it does (the reliable sequence that was missing before):
kills stale wpa_supplicant instances bound to the client iface; removes our
ctrl dirs; rfkill unblock wifi; sets the iface unmanaged in NetworkManager;
creates wlan1 on the base phy if needed; ip link down/up with a settle sleep;
power_save off; verifies exactly one managed interface and no supplicant.

PASS: "RESET DONE" and iface type=managed. A second managed interface that
cannot be created (-EBUSY) is a host limitation: rerun all host phases with
--use-base (cleanup reconnects the uplink).

### C2 - RF gate: scan + antenna alignment

    sudo python3 scripts/assoc_e2e.py scan --profile open     # or wpa2/wpa3
    # antenna alignment loop (no association):
    sudo python3 scripts/assoc_e2e.py watch --scan-only --iterations 20 --interval 3

PASS (RF gate): SSID found, 2437 MHz, BSSID valid, RSN matches the profile, and
**RSSI >= -70 dBm**. Target: -55 dBm or better; the recorded no-antenna level
was -63..-65 dBm (beacon -62 dBm in a monitor capture), so attaching/aligning
antennas should visibly improve the RSSI number in the watch CSV
(build/wifi-assoc-e2e/<stamp>/watch.csv).

* If the AP is not found on wlan1 at all but the monitor/base-interface scans
  see it (the prior wlan1 quirk), rerun with --use-base. The harness already
  tries active -> passive -> full-band before concluding FAIL.
* If RSSI < -70 dBm: STOP the association rungs; fix RF first (antennas,
  orientation, distance). No amount of driver debugging matters below the gate.

### C3 - association attempt (one supplicant, full classification)

    sudo python3 scripts/assoc_e2e.py assoc --profile open --timeout 45
    sudo python3 scripts/assoc_e2e.py assoc --profile wpa2 --timeout 60
    sudo python3 scripts/assoc_e2e.py assoc --profile wpa3 --timeout 90

Each command runs ONE dedicated wpa_supplicant (-D nl80211, own ctrl socket,
-d log, freq_list=2437, p2p_disabled=1), polls wpa_cli status + the log, then
stops the supplicant and writes a JSON classification: COMPLETED,
AUTH-TIMEOUT, SAE-AUTH-TIMEOUT, ASSOC-TIMEOUT, FOURWAY-FAILED, DISCONNECTED,
NETWORK-NOT-FOUND or DRIVER-ERROR. Exit code 0 only for COMPLETED; 1 is a
diagnostic result (not a harness failure).

Immediately after each attempt (router console):

    sh /root/assoc-router-fire.sh --stage snapshot

The snapshot is the router half of the evidence; the client JSON/log is the
other half. Both are required to fill a MATRIX.md row.

### C4 - cleanup

    sudo python3 scripts/assoc_e2e.py cleanup

Stops our supplicant, removes the second vif if reset created it, restores
NetworkManager and reconnects wlan0 if --use-base dropped it.

---

## 4. Association rungs and expected evidence

One profile per boot (see R2). The order below is the recommended bench plan:
Boot A validates the association machinery with the open control, Boot B is the
primary 4-way target, Boot C is optional. A hostapd restart inside a boot is
validated for AP-ENABLED (E2), but it is not used here because the RX arming is
one-shot per load and must stay verifiable (R3). If a profile restart is
absolutely needed: restart hostapd first, then run --stage dp again ONLY if the
RX bank can still be validated (dp_rx_arm refuses -EALREADY otherwise) - when
in doubt, reboot with the target profile; do not guess.

### R4 - OPEN control rung (proves association without any crypto)

Router: AP profile open (R2) + DP (R3). Host: C0 -> C1 -> C2 -> C3 --profile open.

Expected chain:

| # | where | evidence |
| --- | --- | --- |
| 1 | router dmesg | cfg80211: mgmt RX delivered (subtype 0x0b ch 6 2437 MHz rssi ... len 30) to the AP wdev |
| 2 | router dmesg | frame post 'mgmt_tx': g0 id6 total 86 B ... coalesced announce consumed after 0 polls; posted N/32 |
| 3 | router dmesg | cfg80211: mgmt RX delivered (subtype 0x00 ...) (association request) |
| 4 | hostapd log | wlan0: STA <mac> IEEE 802.11: authenticated / associated (aid 1) |
| 5 | router dmesg | frame post 'sta_user_add': g0 id7 total 192 B ... consumed 0 polls (180-B body) |
| 6 | router dmesg | frame post 'sta_asoc_syn': g0 id1 total 16 B ... (cmd 0x07d2, asoc state 1) |
| 7 | router dmesg | sta_ops: STA <mac> added (AID 1, vap 0, band 0): firmware context committed |
| 8 | router dmesg | frame post 'mgmt_tx' ... (association response) + hostapd: AP-STA-CONNECTED |
| 9 | host | assoc JSON: stage=COMPLETED, complete=true |
| 10 | router | golden page/mask/staging after the rung |

No EAPOL is expected in the open rung (nothing to negotiate). This rung proves
mgmt TX/RX + station ops + the client sequence; it is the gate for everything
below. If it fails, do not attempt WPA2/WPA3 - diagnose with DIAGNOSIS.md.

### R5 - WPA2-PSK rung (the primary 4-way target)

Fresh boot (Boot B), AP profile wpa2 from the start:

    sh /root/assoc-router-fire.sh --full --load /root/hi5622_assoc.ko --profile wpa2
    # that runs check -> load -> boot -> C2 -> AP(wpa2) -> dp -> drain-start -> snapshot

Host: C0 -> C1 -> C2 -> C3 --profile wpa2. Expected after the association
chain of R4 (items 1-8 now with the WPA2 RSN IE):

| # | where | evidence |
| --- | --- | --- |
| 1 | router dmesg | cfg80211: add_key posted (g0 id1 cmd 0x0078 'key_add', ... idx=0 pairwise=1 cipher=0xfac04 class=pairwise key_len=16 ...) (PTK, after msg2) |
| 2 | hostapd log | wlan0: STA <mac> WPA: pairwise key handshake completed (RSN) |
| 3 | router dmesg | data_tx: EAPOL-Key msg1/4 TX COMPLETED (peer <mac>, ... B MPDU, protected=0 ...) |
| 4 | router dmesg | data_tx: EAPOL-Key msg3/4 TX COMPLETED (... protected=1 ...) |
| 5 | router cfg80211_status | data-tx tx: posts>=2 completed>=2 ... ; sta-ops wire: rx frames>=2 eapol>=2 delivered>=2 |
| 6 | host | assoc JSON stage=COMPLETED, complete=true; iw link: Connected to 06:05:04:03:02:01 signal ... |
| 7 | router dmesg | cfg80211: set_default_key posted (g0 id1 cmd 0x0079 ... idx=1 uni=0 multi=1) (GTK; hostapd already installed it at AP start, the pairwise add_key is the new one) |
| 8 | router | golden page/mask/staging |

Note: the driver deliberately does NOT use the EAPOL TX-completion shape to
authorize a station (it matches msg1 in the host-SME flow); hostapd's
.change_station AUTHORIZED is the authority.

### R6 - WPA3-SAE rung (optional, image-dependent)

Requires a hostapd built with CONFIG_SAE (final image #2 / Image B hostapd).
If hostapd refuses the config (invalid key_mgmt SAE), this is an IMAGE blocker:
record NOT-RUN and do not debug the driver.

Router: AP profile wpa3 + the same DP order. Host: C3 --profile wpa3 (the
harness sends SAE with sae_pwe=2, ieee80211w=2, MFP-required AP).

Expected: the auth exchange is SAE (auth subtype 0x0b, alg 3) and appears as
mgmt RX (2 auth frames from the client: commit + confirm) and mgmt TX (2 auth
responses) before the assoc exchange, then the same EAPOL 4-way as R5, plus
PMF: hostapd installs the IGTK/set_default_mgmt_key at AP start (already proven)
and uses BIP-CMAC-256 for protected management frames. Client stage SAE is
classified as SAE-AUTH-TIMEOUT if no SAE response arrives (mgmt TX/RX or RF),
distinct from the PSK AUTH-TIMEOUT.

### R7 - data plane proof after association (optional)

Give the link an IP on both ends (no DHCP server on the bench AP):

    # router console
    ip addr add 192.168.77.1/24 dev wlan0
    # host
    sudo ip addr add 192.168.77.2/24 dev wlan1     # or wlan0 with --use-base
    ping -c 3 192.168.77.1
    # router
    ping -c 3 192.168.77.2

PASS: ping replies both ways; router data_tx_status posts advance; cfg80211_status
sta-ops rx counters advance (ARP/data frames consumed by dp_rx_drain and
delivered to the AP netdev). Broadcast ARP is allowed; unicast to the station is
allowed once hostapd marked it authorized.

---

## 5. Evidence collection, matrix and stop rules

For every rung capture:

* router: sh /root/assoc-router-fire.sh --stage snapshot (prints + writes
  /tmp/assoc-e2e/snapshot.txt: boot page/mask/staging, hcc_status, dp_status,
  cfg80211_status, data_tx_status, hostapd tail, dmesg association markers).
* host: the command output + build/wifi-assoc-e2e/<stamp>/ (scan JSON/log,
  supplicant.log, assoc-result.json, iw-link.txt, client-dmesg-tail.txt,
  watch.csv).
* write the MATRIX.md row: rung, PASS/FAIL, first failing stage, evidence
  pointer, classification from DIAGNOSIS.md.

Global STOP rules (unchanged from the project):

* page/mask/staging not golden, or an exception -> physical power-cycle. The
  SoC reboot does NOT reset the Wi-Fi chip.
* an H2D announce not consumed / H2D lane closed -> STOP (one idea per load).
* hostapd fails AFTER the driver posted a frame (dirty failure) -> STOP; a
  host-side-only failure (clean) may be retried.
* never retry an association attempt by reloading the module in the same boot;
  use one supplicant attempt per client rung and the harness's built-in retry.

---

## 6. Appendix - exact expected strings (copy/paste greps)

Router dmesg (corrected association module):

    frame post 'mgmt_tx': g0 id6 total 86 B in SR0 slot ... coalesced announce consumed after 0 polls
    cfg80211: mgmt RX delivered (subtype 0x0b ch 6 2437 MHz rssi -65 len 30) to the AP wdev
    cfg80211: mgmt RX delivered (subtype 0x00 ...
    frame post 'sta_user_add': g0 id7 total 192 B
    frame post 'sta_asoc_syn': g0 id1 total 16 B
    sta_ops: STA %pM added (AID 1, vap 0, band 0)
    DP setup: OK - rings 256/512/4095
    DP txq_sync: g0 id1 key 0x0320 sent
    DP rx_arm: live bank CA 0x40050000 status=0x00000182
    data_tx: EAPOL-Key msg1/4 TX COMPLETED
    data_tx: EAPOL-Key msg3/4 TX COMPLETED
    cfg80211: add_key posted (g0 id1 cmd 0x0078 'key_add'
    cfg80211: set_default_key posted (g0 id1 cmd 0x0079 'key_default'
    cfg80211: start_ap beacon_static: 'ap_vap_start' posted after ap_cfg (channel=6) - vendor AP-start order complete
    wlan0: AP-ENABLED

Hostapd (INFO level, -t timestamps):

    wlan0: AP-ENABLED
    wlan0: STA <mac> IEEE 802.11: authenticated
    wlan0: STA <mac> IEEE 802.11: associated (aid 1)
    wlan0: AP-STA-CONNECTED <mac>
    wlan0: STA <mac> WPA: pairwise key handshake completed (RSN)
    wlan0: STA <mac> WPA: SAE authentication completed      (WPA3 rung)

Host client (wpa_supplicant -d log + wpa_cli status):

    wlan1: SME: Trying to authenticate with 06:05:04:03:02:01 (SSID='...' freq=2437 MHz)
    wlan1: Trying to associate with 06:05:04:03:02:01
    wlan1: Associated with 06:05:04:03:02:01
    wlan1: WPA: Key negotiation completed with 06:05:04:03:02:01 [PTK=CCMP GTK=CCMP]
    wlan1: CTRL-EVENT-CONNECTED - Connection to 06:05:04:03:02:01 completed [id=0 id_str=]
    wpa_state=COMPLETED

Negative shapes the harness classifies (see DIAGNOSIS.md):

    wlan1: Authentication with 06:05:04:03:02:01 timed out.          -> AUTH-TIMEOUT / SAE-AUTH-TIMEOUT
    wlan1: Association with 06:05:04:03:02:01 timed out.             -> ASSOC-TIMEOUT
    wlan1: WPA: 4-Way Handshake failed - pre-shared key may be incorrect -> FOURWAY-FAILED
    wlan1: CTRL-EVENT-NETWORK-NOT-FOUND                              -> NETWORK-NOT-FOUND
