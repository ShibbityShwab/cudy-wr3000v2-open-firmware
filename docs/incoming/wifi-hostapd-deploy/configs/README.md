# configs/ - hostapd fixtures for the AP-start ladder

Both files are **host-side bench fixtures** for
[../AP-START-LADDER.md](../AP-START-LADDER.md); the deploy installs them to
`/root/hostapd-deploy/` on the box (mode 600).  No real credential is
present: the WPA2 passphrase is a placeholder that must be replaced with a
throw-away lab value if it is ever used (it is the negative fixture).

| file | purpose | expected driver behaviour today |
| --- | --- | --- |
| `hostapd-open.conf` | 2.4 GHz ch 6, **OPEN** network, ssid `wr3000v2-apstart`, beacon_int 100, DTIM 2 | **positive**: NEW_BEACON -> cfg80211 start_ap -> provider posts `ap_ssid` (g0 id1 key 0x0005, 64 B) + `ap_cfg` (g0 id1 key 0x0127, 184 B = 156-byte body); needs the AP-type VAP + the mode-1 VAP base (see the ladder) |
| `hostapd-wpa2.conf` | 2.4 GHz ch 6, WPA2-PSK/CCMP | **negative**: the start_ap sequence runs, then hostapd stops at `add_key` -EOPNOTSUPP (driver op still M3); records the exact failure for the later M3 work |

Validity: both parse cleanly with the tree-source hostapd v2.12
(`../tools/validate-configs.sh`, including a broken-config negative control).
They are also semantically checked by `../tools/check-ladder.py` (interface,
band, channel, beacon interval, DTIM, open-vs-WPA2).

The canonical bench command is:

    hostapd -B -i wlan0 -P /root/hostapd-deploy/hostapd.pid /root/hostapd-deploy/hostapd-open.conf

Do not enable the `/etc/init.d/wpad` service: the bench must not autostart an
AP daemon (the deploy gate asserts no `/etc/rc.d` wpad link exists).
