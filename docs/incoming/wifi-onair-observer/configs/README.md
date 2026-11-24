# configs/ - host-side observer fixtures (lab-only)

These are **host wpa_supplicant** fixtures for the observer machine, not AP
fixtures for the router. They are parser-validated by
`tests/selftest.py` and never applied to any network automatically.

| file | purpose | expected today |
| --- | --- | --- |
| `wpa_supplicant-wpa2.conf` | WPA2-PSK association attempt against `cudy-ap-test` (2.4 GHz ch 6, CCMP) | **expected fail at AUTH** (AP-side mgmt-tx blocker); if it completes, the 4-way handshake + link result are recorded |
| `wpa_supplicant-wpa3-sae.conf` | WPA3-SAE + PMF attempt (requires the AP restarted with the WPA2+WPA3 transition fixture) | **expected fail at SAE/AUTH** (no mgmt TX/RX path) |

## Credential boundary

The passphrase `test-passphrase-121` is the **published bench test value**
already used by the AP-side fixture
(`../../wifi-ap-start/recipes/hostapd-wpa2.conf`) and by the
2026-11-28 WPA2-AP GTK bench fire. It is not a secret and not a credential for
any production network. The stock-firmware Wi-Fi password, per-unit PSKs,
`bdinfo` secrets and any key material are out of scope: never put them in a
fixture, an env file, a command line or an evidence log.

The harness copies the fixture to a root-only temporary file outside the repo
when `ONAIR_PSK` overrides the passphrase, and redacts the `psk=` line from
the evidence copies it keeps.
