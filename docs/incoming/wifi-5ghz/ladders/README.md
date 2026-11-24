# ladders/ - fixtures for TEST-LADDER G3/G5

| file | rung | notes |
| --- | --- | --- |
| [hostapd-5ghz-open.conf](hostapd-5ghz-open.conf) | G3 | 5 GHz ch36, 20 MHz, OPEN; isolate the band-enable + AP-start path |
| [hostapd-5ghz-wpa2.conf](hostapd-5ghz-wpa2.conf) | G5 | 5 GHz ch36, WPA2-PSK/CCMP; passphrase placeholder (never commit a real one) |

Both run on an AP interface created on the **5 GHz wiphy**:

    iw phy <5G-phy> interface add wlan5 type __ap
    ip link set wlan5 up
    /usr/sbin/hostapd -dd -t /tmp/hostapd-5ghz-open.conf

Identify the 5 GHz phy with `iw phy` (TEST-LADDER G1/G2).  The on-air
observer step (G4) is the wifi-onair-observer lane's harness plus a manual
`iw dev <observer> scan freq 5180`; see TEST-LADDER G4.
