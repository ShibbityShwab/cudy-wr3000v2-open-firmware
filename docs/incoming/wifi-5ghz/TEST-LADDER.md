# TEST-LADDER - 5 GHz AP start + on-air observer scan (NOT FIRED)

**Lane:** wifi-5ghz (task-145).  **Status: DESIGNED, NOT FIRED.**  Every rung
below is host-designed; the Lead owns the bench, the console, the recovery
door and the on-air observer (lane wifi-onair-observer, task-142).  This lane
never contacted the router.

Prerequisite reading: `BENCH-FACTS.md` (physical invariants), `RUNBOOK.md`
(procedure), the final all-patch fire recipe in
`../wifi-stability/FINAL-INTEGRATION.md` sec 4, and the AP-start flow in
`../wifi-ap-start/HOSTAPD-BRINGUP.md`.  The module under test is the FINAL
ALL-PATCH tree + this lane's patch (`tools/build-band5g.sh`): stripped
312,432 B sha256 `2bee4391...`, **48 params**, not yet fired as this exact
combination.

**Non-negotiable stop rules (inherit BENCH-FACTS/LESSONS):**

* recovery door armed, UART capture read-only running, bench link PASS before
  any fire; no human at the bench = no fire;
* one committed AP start per module load (the stability gate refuses a second);
  an exception mask / page change / mask != 0 -> power-cycle, do not retry in
  place;
* never `rmmod`, never write CA 0x400392f0, never read 0x10161000, no UART
  transmit, no blind flash;
* every AP frame still passes the inherited fail-closed post gate
  (`allow_frames` budget); `band5g` does not bypass it;
* 5 GHz rungs never run before the G1 negative control PASSes (that proves
  the gate is live and the 5 GHz wiphy is identifiable).

---

## G0 - preconditions (no new write)

1. Bench image with the patched module, cfg80211 package, hostapd-mini +
   wifi-scripts ucode (see wifi-ap-start HOSTAPD-BRINGUP sec 1), regdb.
2. Golden boot + PD4 firmware: page `0x86807FEF`, state 1, mask 0, staging 0.
3. HCC ladder to ACTIVE (prepare/upload/verify/seed/release + C2 ctx-create +
   TXQ sync) exactly as the final fire recipe; `h2d_send_budget=10`,
   `allow_frames=32`.
4. Observer host ready (wifi-onair-observer preflight; its own uplink guarded).

Expected: unchanged from the bench-validated final module - this rung adds
nothing.

## G1 - identify the 5 GHz wiphy + negative gate (control, 2.4 GHz only writes)

Load the module with `band5g=0` (default) and the proven final fire params:

    insmod /lib/modules/$(uname -r)/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 band5g=0 \
        sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0

Then (read-only):

    iw phy | grep -E '^Wiphy|Band [0-9]'
    for p in /sys/class/ieee80211/phy*; do echo "== $p"; \
      iw phy $(basename $p) info | sed -n '/Band /,/^$/p'; done

PASS criteria:

* exactly **two wiphys**; one advertises the 2.4 GHz band only (13/14 ch), the
  other the 5 GHz band only (19 LU channels: 36..140, because `band5g=0`);
* the second wiphy does **not** advertise VHT (band5g off, defaults);
* cfg80211_status shows `band5g=0`:
  `cat /sys/class/device/.../cfg80211_status` (or the sysfs path used by the
  earlier lanes).

Negative gate (must be refused, no frame):

    iw phy phyN interface add wlan5 type __ap   # N = the 5 GHz wiphy
    ip link set wlan5 up
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-open.conf   # channel 36
    dmesg | grep -E 'start_ap REFUSED - band 1'

PASS = hostapd reaches `.start_ap` and the driver refuses with
`start_ap REFUSED - band 1 ... 5 GHz AP needs band5g=1` -> no VAP base, no
0x0124, no AP config on the wire, page golden unchanged.  **Any frame or page
change here is a FAIL** (band5g gate broken).  Remove `wlan5` afterwards
(`iw dev wlan5 del`).  This rung is 5 GHz-config-free; the only writes are
the normal boot/C2 ladder (already fused on this image).

## G2 - 5 GHz wiphy profile (`band5g=1`, no AP yet)

Load the same module with `band5g=1`.  Do NOT start the AP in this rung if a
module reload is not possible; G2/G3 can share one load.

Read-only checks:

    iw phy <5G-phy> info

PASS criteria (dmesg + iw):

* dmesg: `cfg80211: band5g=1 5GHz wiphy profile active: 29 channels ...
  HT cap 0x0062, VHT cap 0x00400064, MCS map 0xfffa`;
* the 5 GHz wiphy lists **29 channels** including ch144 and 184/188/192/196
  (channels disabled by the kernel regdb are expected - the list must
  *contain* them; the l
  "disabled" flag path is the regdb, not the table);
* HT + VHT flags appear (hostapd may now advertise VHT);
* 2.4 GHz wiphy unchanged (13 ch, baseline caps).

## G3 - 5 GHz open AP start (the core rung)

Keep `band5g=1` loaded.  Re-run the proven prepare ladder until ACTIVE, then:

    # 5 GHz wiphy only
    iw phy <5G-phy> interface add wlan5 type __ap
    ip link set wlan5 up
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-open.conf 2>&1 | tee /tmp/hostapd-5g.log

Config: [ladders/hostapd-5ghz-open.conf](ladders/hostapd-5ghz-open.conf)
(hw_mode=a, channel=36, 20 MHz, OPEN, beacon_int 100).

PASS criteria (all of):

* dmesg vendor order with band=1: `ap_chan` posted with
  `band=1 channel=36`, then `ap_ssid`, `ap_cfg`, `ap_vap_start`
  (beacon_static=1), no firmware exception;
* hostapd `wlan5: AP-ENABLED`, SSID `cudy-ap-test-5g`, type AP;
* VAP-object/channel-context readback lines are log-only (never gating, per
  beacon-static bench correction);
* page/state/mask unchanged after the start (mask 0).

If hostapd refuses at channel selection: re-check G2 (29-channel list) and
the regdb (`iw reg get`); ch36 is non-DFS and should be allowed in any
domain.

STOP: any exception mask / page change -> power-cycle, keep /tmp/hostapd-5g.log
+ UART capture, no retry.

## G4 - on-air observer scan (decisive on-air proof)

Observer host (wifi-onair-observer lane; the same host that runs the harness):

    # preferred: the lane harness (its runner supports a channel/SSID override;
    # if not, the manual path below is equivalent and read-only):
    sudo tools/iface-up.sh
    sudo iw dev wlan1 scan freq 5180 | tee /tmp/scan-5g.txt

Explicit 5 GHz AP: SSID `cudy-ap-test-5g`, BSSID = VAP MAC, freq 5180
(channel 36), DS param 36, beacon interval 100, HT/VHT IEs when hostapd
advertises them.

PASS criteria:

* the BSS appears in the observer scan on 5180 with the SSID/DS-channel above
  (RSSI is expected to be strong at the bench);
* optional monitor capture (wifi-onair-observer `capture-beacon.sh`) shows
  the 802.11 beacon management frames (subtype 8) at ~100 TU: this is the
  on-air proof that the firmware beacon engine is transmitting on 5 GHz;
* 2.4 GHz control AP (if still up on the other wiphy) is NOT visible on 5180
  (band isolation sanity).

EXPECTED-TODAY (documented limits): the beacon is expected to appear but
client **association is still blocked** by the host-software AP mgmt path
(`.mgmt_tx` fail-closed, no mgmt RX) - the same blocker as 2.4 GHz, not a
5 GHz-specific failure.  Record it as the documented EXPECTED-FAIL (L3 in
wifi-onair-observer), not as a 5 GHz regression.

## G5 - 5 GHz WPA2 AP (key path cross-check)

Only after G3/G4 PASS.  Restart hostapd with
[ladders/hostapd-5ghz-wpa2.conf](ladders/hostapd-5ghz-wpa2.conf)
(WPA2-PSK/CCMP, placeholder passphrase; substitute the lab fixture at the
bench, never commit it).

PASS criteria:

* dmesg WPA2 GTK install: `key_add` g0 id1 0x0078 96 B idx=1 multi cipher
  0xfac04 and `set_default_key` 0x0079 68 B (the hardware-proven 2.4 GHz
  path, now with band=1);
* hostapd AP-ENABLED; client 4-way still blocked by the mgmt/RX lanes
  (EXPECTED-FAIL, same classification as 2.4 GHz).

## G6 - H1 band config on 5 GHz (experimental, one load)

Only if a fire is available after G4; requires `scan_band_cfg` enabled and
the 5 GHz wiphy active.  This is the rung that exercises the fixed
channel-number derivation.

    insmod ... band5g=1 scan_band_cfg=1 scan_band_cfg_chan=0 \
        scan_band_cfg_slot=0 scan_band_cfg_start=12 scan_band_cfg_field10=13 \
        scan_band_cfg_mode=2
    iw dev wlan5 scan freq 5180        # or 36/40

PASS criteria:

* dmesg `H1 band-config subtype1 sent (chan=36 ...)` - i.e. the channel
  was derived as **36**, not forwarded as 5180 (the old behaviour that the
  firmware resolver cannot resolve);
* either the scan re-arms / a second request produces channel results (the
  H1 hypothesis) or it fails cleanly with
  `firmware did not re-arm the scan context (H1)` - either is a valid
  recorded result; the decisive datum is the resolved channel number and the
  absence of a firmware exception;
* 5 GHz H1 with `band5g=0` is refused (-EOPNOTSUPP) - negative control.

Because `scan_band_cfg` semantics are still experimental ([I], scan-tuning
R4), G6 runs only if the fire budget allows; it is not a precondition for the
AP milestone.

---

## Evidence to collect (Lead, per rung)

* dmesg (full, from module load to the rung's end) + UART capture;
* `/tmp/hostapd-5g.log` and the observer `/tmp/scan-5g.txt` (or the
  wifi-onair-observer run directory/pcap, local only);
* `cfg80211_status` + `iw phy` band dumps before/after;
* page/state/mask readback after every rung;
* result classification: rung PASS/FAIL/EXPECTED-FAIL, with the first failing
  dmesg line.

## PASS/FAIL matrix (design expectation)

| rung | design expectation | decisive evidence |
| --- | --- | --- |
| G1 identify + negative gate | PASS | 5 GHz AP start refused with `band5g=0`, no frame/page change |
| G2 `band5g=1` profile | PASS | 29-channel list + HT 0x62 + VHT 0x400064 in `iw phy` |
| G3 5 GHz open AP | PASS expected | hostapd AP-ENABLED on phy5 + ap_chan band=1 ch36 + golden page |
| G4 observer scan | PASS expected (beacon on air) | 5180 scan shows `cudy-ap-test-5g`; monitor capture of beacons |
| G4b client assoc | EXPECTED-FAIL | auth/assoc blocked by mgmt-tx/RX lanes (not 5 GHz-specific) |
| G5 5 GHz WPA2 GTK | PASS expected for key install | 0x0078/0x0079 band=1; 4-way still EXPECTED-FAIL |
| G6 H1 5 GHz channel number | EXPERIMENT | `chan=36` (not 5180); re-arm result recorded |
