# HOSTAPD-BRINGUP - real AP fire: hostapd integration gaps, fixes and exact flow

**Lane:** wifi-ap-start (task-121), host-only, 2026-11-26 (v2, after the
bench's first real hostapd fire).  The bench router was not contacted from
this lane; the lead's fire produced the observations below.

## 1. What the real fire found (lead, 2026-11-26)

| # | observation | root cause | status |
| --- | --- | --- | --- |
| 1 | hostapd-mini fails at startup: "Error loading ucode: Unable to resolve path for module 'common'" | the image ships hostapd-mini but not the OpenWrt wifi-scripts package (its /usr/share/hostap/*.uc modules) | bench workaround pushed by the lead; image fix documented in [../image/wifi-scripts-ucode.md](../image/wifi-scripts-ucode.md) and [tools/push-hostapd-ucode.sh](../tools/push-hostapd-ucode.sh) |
| 2 | with ucode present: "nl80211: Driver does not support authentication/association or connect commands; ... driver initialization failed; wlan0: AP-DISABLED" | cfg80211 advertises NL80211_CMD_AUTHENTICATE only when cfg80211_ops.auth exists, and NL80211_CMD_CONNECT only when cfg80211_ops.connect/auth exists (net/wireless/nl80211.c nl80211_add_commands_unsplit).  hostapd's shared init gate (driver_nl80211_capa.c wpa_driver_nl80211_get_info) errors out when neither is advertised - before any AP op. | **fixed in module v2**: a fail-closed .connect op is registered so NETLINK advertises CONNECT; hostapd's AP path never calls it |
| 3 | .start_ap was the M3 stub (-EOPNOTSUPP) | the fired module predated this lane's provider patch (no AP_START capability) | **fixed in module v2**: the AP-start provider patch is in the built module; .start_ap posts the 4-message vendor sequence (g0 carrier default: ap_ssid -> ap_cfg) |
| 4 | hostapd may need the interface switched to AP (SET_INTERFACE) | hi5622_cfg11_change_virtual_intf refused every type change | **fixed in module v2**: STATION<->AP<->MONITOR changes are accepted by the M1 netdev model (cfg80211 core updates the wdev iftype); preferred flow still creates the interface as AP directly |
| 5 | vendor wpad binary unusable (HiSilicon private ioctls, monitor iface -95) | vendor binary, not our driver | not a target: use hostapd-mini |
| 6 | cipher suites | already advertised: WEP40/WEP104/TKIP/CCMP/GCMP/AES_CMAC/BIP_CMAC_256 (wiphy->cipher_suites) | no change needed |
| 7 | interface combinations for AP | not needed by cfg80211 for a single AP: cfg80211_iftype_allowed(..., check_swif=0) checks wiphy->interface_modes, which already has AP|STATION|MONITOR; NEW_INTERFACE for type __ap is accepted | documented, no combinations added (they would only constrain) |

## 2. Module v2 (built by this lane, host-only)

    artifact : /tmp/wifi-ap-start-bench-artifact/hi5622_wifi.ko
    size     : 199,344 B
    sha256   : f9175ee3d2143429ce071ddce4103c0c8eabd14a173841ccdb253ecfb0754dc5
    vermagic : 6.18.60 SMP mod_unload ARMv7 p2v8
    depends  : cfg80211

The artifact is **debug-stripped** (the kernel tree has DEBUG_INFO on, so the
raw .ko embeds the build path and its sha varies with the work directory).
The stripped module is path-independent and keeps .symtab/.rel.* needed for
loading and EXPORT_SYMBOL; build-bench-module.sh prints exactly this size/sha.
    content  : wifi-scan-ap src + wifi-vap-start builder/provider patch
               + wifi-ap-start builder/provider patch v2
               (caps AP_START, AP-start sequence in .start_ap, .connect gate,
                AP iftype change, ap_start_mode param)

Reproduce: [../tools/build-bench-module.sh](../tools/build-bench-module.sh)
(prints size/sha; the .ko is local evidence only and is never committed).

Copy to the box (host side, attended session):

    tools/rscp.sh /tmp/wifi-ap-start-bench-artifact/hi5622_wifi.ko root@192.168.1.1:/tmp/

Hostapd ucode (bench workaround, if not yet pushed):

    opensource/docs/incoming/wifi-ap-start/tools/push-hostapd-ucode.sh --apply

## 3. Exact hostapd flow on the bench

Preconditions: cfg80211 bench image + PD4 golden boot, recovery door armed,
UART capture running, bench link PASS.  The AP start sequence posts H2D frames,
so the HCC core must be ACTIVE (ctx-create + TXQ sync) before hostapd starts.

    # 1. modules (cfg80211 first; allow a budget for carrier + AP frames)
    insmod /tmp/cfg80211.ko
    insmod /tmp/hi5622_wifi.ko allow_frames=8 frame_gap_ms=100 h2d_send_budget=8

    # 2. run the proven boot/C2 ladder (existing bench tooling) until the
    #    provider post gate reports ACTIVE; confirm the golden page + mask 0
    #    phase_matrix.py --phase C2   (or the manual ladder used so far)

    # 3. regulatory + AP interface (regdb is installed from the T0 userspace
    #    deploy; the world/00 regdomain is NO_IR, so set a real country)
    iw reg set US
    iw dev wlan0 del 2>/dev/null || true
    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up

    # 4. hostapd-mini, foreground, verbose; config pushed to /tmp first
    /usr/sbin/hostapd -dd -t /tmp/hostapd-open.conf 2>&1 | tee /tmp/hostapd.log

Config: [../recipes/hostapd-open.conf](../recipes/hostapd-open.conf)
(2.4 GHz ch 6, OPEN, no HT/WMM: the first rung needs no add_key, no txq
params and no HT capabilities in the wiphy).  Keep it running in the
foreground; Ctrl-C stops the AP (stop_ap is a separate provider capability,
not part of this rung).

## 4. Expected log / dmesg

hostapd (nl80211 init):

    nl80211: Driver does not support ...        <- MUST NOT appear (v2 fixes it)
    nl80211: interface wlan0 in phy phy0
    nl80211: Setup of wlan0 completed
    wlan0: Setup of interface done
    wlan0: AP-ENABLED

driver (dmesg):

    cfg80211: type change 2 -> 3 accepted by the M1 netdev model ...   (only
        when hostapd switches a managed interface to AP)
    cfg80211: start_ap sequence frame 1/4 'ap_ssid' posted (64 B, ...)
    cfg80211: start_ap sequence frame 2/4 'ap_cfg' posted (184 B, ...)
    cfg80211: start_ap sequence posted to provider '...' (2 frame(s)) - EXPERIMENTAL

(the frame 1/4 + 2/4 numbering is the fixed label array; the g0 carrier only
emits the first two members.  ap_start_mode=1 would emit 4.)

Protocol observations (read-only): the provider post path increments
frames_posted by 2 and every frame is CONSUMED with mask 0, staging 0.

## 5. Readback to expect (corrected ctx0 = BAR0 0x7D1758 for this boot)

ctx0BAR0 = module-table slot0 target (BAR0 0x829458) + base offset u16
(BAR0 0x82A504); on the lead's boot that resolved to **0x7D1758**.

| read | address | expected |
| --- | --- | --- |
| exception mask | CA 0x40008 | 0 |
| staging frame | BAR0 0x6F800C | zero |
| ctx0 flags 0x18C | BAR0 0x7D1758 + 0x18C = 0x7D18E4 | the low byte CHANGES after ap_cfg is consumed (AP/HT mode bits set by firmware worker 0x84DA4) |
| ctx0 flags 0x18D | BAR0 0x7D1758 + 0x18D = 0x7D18E5 | bit0 = hidden SSID (0 for the open config), bit2 = band/mode flag |
| VAP pointer | BAR0 0x7D1758 + 0x190 = 0x7D18E8 (u32) | nonzero after the mode-1 VAP base; it is a device CA V |
| VAP +0x10 | BAR0 = V + 0x6B8000 + 0x10 | u32 100 (0x00000064) = beacon interval |
| VAP +0x14 | BAR0 = V + 0x6B8000 + 0x14 | u32 2 (0x00000002) = DTIM |
| VAP +0x64 | BAR0 = V + 0x6B8000 + 0x64 | flags cleared/set by the AP-config worker |

The VAP pointer dereference window (CA + 0x6B8000) is [I] until this fire
confirms it; ctx0+0x18C/0x18D need no dereference and are the first-PASS gate.
If V + 0x6B8000 is out of the BAR0 aperture, record the raw V value and stop -
do not guess an alternative window.

## 6. Failure table (what to do if it still fails)

| log | meaning | action |
| --- | --- | --- |
| "Driver does not support authentication/association or connect commands" | module without the v2 .connect gate (old .ko) is loaded | verify sha256 aba5c02e..., insmod the v2 module |
| "Could not configure driver mode" | wlan0 is managed and change_virtual_intf refused (old module) OR interface busy | use the v2 module; or create wlan0 with type __ap directly |
| ".start_ap ... -EOPNOTSUPP" | module has no AP_START provider (old .ko) or ap_start_mode invalid | verify v2 module; ap_start_mode must be 0 or 1 |
| "frame post 'ap_ssid' REFUSED (...)" | allow_frames=0/exhausted, core not ACTIVE, wire not ready, out[0] pending | check module params, run the C2 ladder first, do not retry in the same boot |
| start_ap returns -EINVAL from cfg80211 | regulatory: channel has NO_IR (world regdomain) or bad chandef | iw reg set US (regdb), re-check iw phy phy0 info flags |
| "Hardware does not support configured mode" / HT errors | hostapd config enabled HT/WMM | use the OPEN/no-HT config first |
| hostapd stops at add_key | WPA2 config (keys are M3, no add_key op yet) | first rung is OPEN; WPA2 waits for the key path |

Stop rules: exception mask non-zero, non-zero staging, golden page/state or
g+0xB4/+0xB8 change, any refused frame, any dmesg fault -> record, power-cycle,
analyse offline; never re-send in the same boot.  Never write CA 0x400392F0,
never post mailbox id 5, never send group 1/4.

## 7. Known gaps / next

* WPA2 key installation (add_key / set_key) is M3: the OPEN rung is the
  supported first fire; [../recipes/hostapd-wpa2.conf](../recipes/hostapd-wpa2.conf)
  documents the target config for that later rung.
* SET_BEACON/change_beacon (running-AP updates) is not registered; only
  NEW_BEACON is wired.  A start_ap sequence uses the AP-config body with
  start=1; change-beacon bodies (start=0) are already supported by the
  builder but need a .change_beacon op plus cached AP state.
* stop_ap is still the provider-gated stub (no STOP capability advertised).
* Data plane (EAPOL / data frames) is a separate lane; an AP-ENABLED hostapd
  is a control-plane milestone, not a usable AP.
* Image: add CONFIG_PACKAGE_wifi-scripts=y so the ucode workaround is not
  needed (image lane).
