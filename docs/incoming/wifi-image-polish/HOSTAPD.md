# HOSTAPD - AP fixtures and the real driver capability matrix

Four fixtures ship in `rootfs/etc/hi5622/`.  **Only the open-network class is
usable today** (it needs no key installation).  WPA2 and WPA2+WPA3 are
hardened, parser-validated targets for the future driver milestones - not
working security.  The driver capability matrix below is the reason.

## Driver capability matrix (what the flashed driver actually implements)

| feature | driver state today | fixture / consequence |
| --- | --- | --- |
| **OPEN AP** | `.start_ap`/`.stop_ap` registered; v7 posts the 4-frame AP-start sequence (ap_ssid 64 B, ap_cfg 184 B, plus VAP add/start 48/40 B).  Hardware-fired 2026-11-27: hostapd **AP-ENABLED**, frames consumed in 0 polls, page/mask golden. | usable.  `hostapd-open.proven.conf` is the fired baseline; `hostapd-open.conf` is the hardened deployment shape (HT20, bigger station limit) and has **not** been fired in that exact form. |
| **HT20** | wiphy advertises an HT20-only skeleton: SGI20 + AMSDU, MCS0-7 (1 stream), AMPDU factor 3. | `ieee80211n=1`, `ht_capab=[HT20][SHORT-GI-20]`.  Parse-validated only (fired baseline had no HT block). |
| **HT40** | **not advertised.**  No HT40 capability bits in the wiphy, no 40 MHz support. | do not add `[HT40+]/[HT40-]`; hostapd would disagree with the driver caps. |
| **2.4 GHz channels** | channels 1-13 advertised (2412-2472 MHz, max 30 dBm); 12/13 depend on the regdomain. | channel 6 in the fixtures.  The image regdomain is the **unsigned world db (NO_IR)** - set a country first (`hi5622.ap.country` / `iw reg set US` in the proven flow). |
| **5 GHz** | second PCI endpoint can be forced 5 GHz (`band_force=1`): 36/40/44/48 no-IR + 52-64 and 100-140 marked DFS/RADAR/INDOOR.  No CAC/DFS engine. | an AP is only conceivable on 36/40/44/48 after band_force=1; **not validated**, and DFS channels cannot be used as AP. |
| **WMM** | no per-STA QoS/U-APSD ops; hostapd builds the WMM IE itself. | `wmm_enabled=1` was part of the fired baseline (AP-ENABLED); U-APSD stays off. |
| **beacon interval / DTIM** | hostapd builds the beacon content; whether/how the firmware emits beacons is **not host-observable** (needs an external observer). | `beacon_int=100`, `dtim_period=2` in all fixtures; no on-air claim. |
| **WPA2-PSK** | **blocked**: struct cfg80211_ops has no `.add_key`/`.set_key` (M3).  cfg80211 returns -EOPNOTSUPP at key installation, i.e. after a successful start_ap frame sequence. | `hostapd-wpa2.conf` is ready (WPA2-PSK/CCMP, rekey, HT20) but cannot complete a handshake. |
| **WPA3-SAE** | **blocked twice**: M3 key install *and* the SAE/management-frame auth path (M4; `.mgmt_tx` is fail-closed, no management RX). | `hostapd-wpa2-wpa3.conf` (transition: WPA-PSK+SAE, PMF required) is a future-ready target only. |
| **PMF / 802.11w** | mgmt frame subscription bits are advertised (hostapd setup needs them), but there is no mgmt RX and no key install. | deferred; the WPA2 fixture keeps `ieee80211w=0` with a commented M3+ block. |
| **station reporting** | `.get_station`/`.dump_station` return -EOPNOTSUPP. | hostapd cannot report associated stations; does not block AP-ENABLED. |
| **association / DHCP** | not verified - no external client has associated. | do not claim a working AP; see REPORT.md limits. |

## Fixture status

| file | status |
| --- | --- |
| `hostapd-open.proven.conf` | **hardware-fired baseline** (AP-ENABLED on the flashed image).  Keep for bisecting; no HT, no WPA. |
| `hostapd-open.conf` | hardened open deployment shape (HT20, max_num_sta=16).  Parser-validated with hostapd v2.12; **not fired** in this form. |
| `hostapd-wpa2.conf` | hardened WPA2-PSK/CCMP.  Parser-validated; expected to fail at `add_key` (-EOPNOTSUPP) today. |
| `hostapd-wpa2-wpa3.conf` | hardened WPA2+WPA3 transition.  Parser-validated (needs a CONFIG_SAE hostapd - the target hostapd-mini supports SAE); **not functional**, needs M3+M4. |

Config validity is enforced two ways in `tools/selftest.sh`:
`tools/validate.py` checks the semantics/capability matrix, and the
SAE-enabled hostapd v2.12 build (`tools/build-hostapd-validator.sh`) parses
every fixture plus a deliberately broken negative control.

## Running an AP

    iw reg set US                      # country is mandatory on the NO_IR world db
    iw phy <phy> interface add wlan0 type __ap
    ip link set wlan0 up
    hostapd -B -t -P /var/run/hi5622-hostapd.pid /etc/hi5622/hostapd-open.conf
    iw dev wlan0 info                  # expect: type AP

To test the WPA2 negative fixture (records the exact M3 failure):

    hostapd -dd -t /etc/hi5622/hostapd-wpa2.conf 2>&1 | tee /tmp/hostapd-wpa2.log
    # expected: AP-start frames posted, then key installation fails with
    # -EOPNOTSUPP (driver add_key absent) - that is the documented M3 gap.

Do not deploy any fixture as a production AP and do not treat hostapd
AP-ENABLED as on-air proof.
