# configs/ - hostapd / wpa_supplicant fixtures for the wifi-scan-ap lane

Shared task **task-113** (host-only). These are userspace fixtures for the
bench ladder in [../TEST-LADDER.md](../TEST-LADDER.md); they contain **no real
credential** (the PSKs are placeholders that must be replaced with a
throw-away lab value at the bench, never committed).

| file | purpose | expected driver behavior today |
| --- | --- | --- |
| `hostapd.conf` | 2.4 GHz ch 6, WPA2-PSK/CCMP AP on `wlan0` | **negative**: `start_ap` is provider-gated (`-EOPNOTSUPP` with no builder; frame post if a builder is registered but `allow_frames=0`), and `add_key` is M3/NULL => hostapd stops at key installation |
| `hostapd-open.conf` | 2.4 GHz ch 6, OPEN AP on `wlan0` | **positive control-plane**: exercises `NL80211_CMD_NEW_BEACON` -> driver `start_ap` without keys; no data path/beaconing proof |
| `wpa_supplicant.conf` | STA association to the WPA2 SSID | **negative**: `connect` family is M5/NULL => `-EOPNOTSUPP`; records the exact userspace failure for the later M3/M5 work |

Why WPA2-CCMP: the wiphy advertises WEP40/WEP104/TKIP/CCMP/GCMP/AES_CMAC/
BIP_CMAC_256 (inherited M0 capability set), and the vendor userspace defaults
to WPA2-CCMP; ch 6 is inside the advertised 1..13 with no DFS/regulatory
entanglement.

Safety: these configs are never installed by the driver build; they are copied
to the bench box manually and only used after the wiphy is registered (the M0
gate). See `../TEST-LADDER.md` for the exact command sequence, expected
observables and stop rules.
