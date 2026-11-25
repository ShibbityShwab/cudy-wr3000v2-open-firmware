# SPDX-License-Identifier: GPL-2.0
"""Synthetic fixtures for the wifi-assoc-e2e host self-checks.

These are hand-written test data in the shape of real iw/wpa_supplicant output
(see ../RUNBOOK.md for the live capture commands). They are NOT hardware
results and carry no credential: the PSK shown is the published lab value.
"""

# --- iw dev wlan1 scan: target AP (WPA2) + one 5 GHz neighbour -------------
IW_SCAN_TARGET_WPA2 = """BSS 6e:05:c3:50:85:7b(on wlan1)
\tlast seen: 801456.450s [boottime]
\tTSF: 185523317737 usec (2d, 03:32:03)
\tfreq: 5200.0
\tbeacon interval: 100 TUs
\tcapability: ESS Privacy ShortPreamble SpectrumMgmt ShortSlotTime RadioMeasure (0x1531)
\tsignal: -36.00 dBm
\tSSID: slowphone
\tRSN:\t * Version: 1
\t\t * Group cipher: CCMP
\t\t * Pairwise ciphers: CCMP
\t\t * Authentication suites: SAE
\t\t * Capabilities: 16-PTKSA-RC 1-GTKSA-RC MFP-required MFP-capable (0x00cc)

BSS 06:05:04:03:02:01(on wlan1)
\tlast seen: 120.500s [boottime]
\tTSF: 123456789 usec (0d, 00:02:03)
\tfreq: 2437.0
\tbeacon interval: 100 TUs
\tcapability: ESS Privacy ShortSlotTime (0x0411)
\tsignal: -63.00 dBm
\tlast seen: 60 ms ago
\tInformation elements from Beacon frame:
\t\t * SSID: cudy-ap-test
\t\t * Supported rates: 1.0* 2.0* 5.5* 11.0* 6.0 9.0 12.0 18.0
\t\t * DS Parameter set: channel 6
\t\t * TIM: DTIM Count 1 DTIM Period 2 Bitmap Control 0x0 Bitmap[0] 0x0
\t\t * RSN:\t * Version: 1
\t\t\t * Group cipher: CCMP
\t\t\t * Pairwise ciphers: CCMP
\t\t\t * Authentication suites: PSK
\t\t\t * Capabilities: 16-PTKSA-RC 1-GTKSA-RC (0x000c)
"""
IW_SCAN_TARGET_WPA2_EXPECT = 2

# --- same AP, far below the RF gate (the "no antennas" ~44 dB case) --------
IW_SCAN_WEAK_WPA2 = IW_SCAN_TARGET_WPA2.replace("signal: -63.00 dBm",
                                                "signal: -83.00 dBm")

# --- target absent: unrelated BSSes only ----------------------------------
IW_SCAN_OTHER = """BSS 9c:fa:96:2e:82:b4(on wlan1)
\tfreq: 2427.0
\tcapability: ESS Privacy ShortSlotTime APSD RadioMeasure (0x1c11)
\tsignal: -72.00 dBm
\tSSID: neighbor-net
\tDS Parameter set: channel 4
\tRSN:\t * Version: 1
\t\t * Group cipher: CCMP
\t\t * Pairwise ciphers: CCMP
\t\t * Authentication suites: PSK
\t\t * Capabilities: 16-PTKSA-RC 1-GTKSA-RC (0x000c)
"""

# --- open AP control rung --------------------------------------------------
IW_SCAN_TARGET_OPEN = """BSS 06:05:04:03:02:02(on wlan1)
\tfreq: 2437.0
\tcapability: ESS ShortSlotTime (0x0401)
\tsignal: -63.00 dBm
\tSSID: wr3000v2-apstart
\tDS Parameter set: channel 6
"""

# --- WPA3-SAE AP: SAE AKM, MFP-required, BIP-CMAC-256 ---------------------
IW_SCAN_TARGET_WPA3 = """BSS 06:05:04:03:02:01(on wlan1)
\tfreq: 2437.0
\tcapability: ESS Privacy ShortSlotTime (0x0411)
\tsignal: -65.00 dBm
\tSSID: cudy-wr3000v2-wpa3
\tDS Parameter set: channel 6
\tRSN:\t * Version: 1
\t\t * Group cipher: CCMP
\t\t * Pairwise ciphers: CCMP
\t\t * Authentication suites: SAE
\t\t * Capabilities: 16-PTKSA-RC 1-GTKSA-RC MFP-required MFP-capable (0x00cc)
\t\t * Group management cipher suite: BIP-CMAC-256
"""

# --- wpa_supplicant = -d log fixtures (2.12 style) ------------------------
WPA2_COMPLETED_LOG = """wlan1: SME: Trying to authenticate with 06:05:04:03:02:01 (SSID='cudy-ap-test' freq=2437 MHz)
wlan1: Trying to associate with 06:05:04:03:02:01 (SSID='cudy-ap-test' freq=2437 MHz)
wlan1: Associated with 06:05:04:03:02:01
wlan1: WPA: Key negotiation completed with 06:05:04:03:02:01 [PTK=CCMP GTK=CCMP]
wlan1: CTRL-EVENT-CONNECTED - Connection to 06:05:04:03:02:01 completed [id=0 id_str=]
"""
WPA2_COMPLETED_STATUS = """wpa_state=COMPLETED
address=14:ac:60:46:83:9e
freq=2437
ssid=cudy-ap-test
bssid=06:05:04:03:02:01
key_mgmt=WPA2-PSK
"""

WPA2_AUTH_TIMEOUT_LOG = """wlan1: SME: Trying to authenticate with 06:05:04:03:02:01 (SSID='cudy-ap-test' freq=2437 MHz)
wlan1: Authentication with 06:05:04:03:02:01 timed out.
"""
WPA2_AUTH_TIMEOUT_STATUS = """wpa_state=DISCONNECTED
ssid=
bssid=
"""

WPA2_ASSOC_TIMEOUT_LOG = """wlan1: SME: Trying to authenticate with 06:05:04:03:02:01 (SSID='cudy-ap-test' freq=2437 MHz)
wlan1: Trying to associate with 06:05:04:03:02:01 (SSID='cudy-ap-test' freq=2437 MHz)
wlan1: Association with 06:05:04:03:02:01 timed out.
"""
WPA2_ASSOC_TIMEOUT_STATUS = """wpa_state=ASSOCIATING
"""

WPA2_4WAY_FAILED_LOG = """wlan1: Trying to associate with 06:05:04:03:02:01 (SSID='cudy-ap-test' freq=2437 MHz)
wlan1: Associated with 06:05:04:03:02:01
wlan1: WPA: 4-Way Handshake failed - pre-shared key may be incorrect
wlan1: CTRL-EVENT-DISCONNECTED bssid=06:05:04:03:02:01 reason=15
"""
WPA2_4WAY_FAILED_STATUS = """wpa_state=DISCONNECTED
bssid=06:05:04:03:02:01
"""

WPA3_AUTH_TIMEOUT_LOG = """wlan1: SME: Trying to authenticate with 06:05:04:03:02:01 (SSID='cudy-wr3000v2-wpa3' freq=2437 MHz)
wlan1: Authentication with 06:05:04:03:02:01 timed out.
"""
WPA3_AUTH_TIMEOUT_STATUS = """wpa_state=DISCONNECTED
"""

WPA2_NOT_FOUND_LOG = """wlan1: CTRL-EVENT-NETWORK-NOT-FOUND
"""
WPA2_NOT_FOUND_STATUS = """wpa_state=DISCONNECTED
"""

DRIVER_ERROR_LOG = """nl80211: failed to set interface wlan1 up: Device or resource busy
wlan1: Failed to initialize driver interface
"""
DRIVER_ERROR_STATUS = """wpa_state=INTERFACE-DISABLED
"""
