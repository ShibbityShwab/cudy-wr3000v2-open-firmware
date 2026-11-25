# HOSTAPD-BRINGUP - real AP fire: hostapd integration gaps, fixes and exact flow

**Lane:** wifi-ap-start (task-121), host-only, 2026-11-26 (v2, after the
bench's first real hostapd fire).  The bench router was not contacted from
this lane; the lead's fire produced the observations below.

## 1. What the real fire found (lead, 2026-11-26)

| # | observation | root cause | status |
| --- | --- | --- | --- |
| 1 | hostapd-mini fails at startup: "Error loading ucode: Unable to resolve path for module 'common'" | the image ships hostapd-mini but not the OpenWrt wifi-scripts package (its /usr/share/hostap/*.uc modules) | bench workaround pushed by the lead; image fix documented in [../image/wifi-scripts-ucode.md](image/wifi-scripts-ucode.md) and [tools/push-hostapd-ucode.sh](tools/push-hostapd-ucode.sh) |
| 2 | with ucode present: "nl80211: Driver does not support authentication/association or connect commands; ... driver initialization failed; wlan0: AP-DISABLED" | cfg80211 advertises NL80211_CMD_AUTHENTICATE only when cfg80211_ops.auth exists, and NL80211_CMD_CONNECT only when cfg80211_ops.connect/auth exists (net/wireless/nl80211.c nl80211_add_commands_unsplit).  hostapd's shared init gate (driver_nl80211_capa.c wpa_driver_nl80211_get_info) errors out when neither is advertised - before any AP op. | **fixed in module v2**: a fail-closed .connect op is registered so NETLINK advertises CONNECT; hostapd's AP path never calls it |
| 3 | .start_ap was the M3 stub (-EOPNOTSUPP) | the fired module predated this lane's provider patch (no AP_START capability) | **fixed in module v2**: the AP-start provider patch is in the built module; .start_ap posts the 4-message vendor sequence (g0 carrier default: ap_ssid -> ap_cfg) |
| 4 | hostapd may need the interface switched to AP (SET_INTERFACE) | hi5622_cfg11_change_virtual_intf refused every type change | **fixed in module v2**: STATION<->AP<->MONITOR changes are accepted by the M1 netdev model (cfg80211 core updates the wdev iftype); preferred flow still creates the interface as AP directly |
| 5 | vendor wpad binary unusable (HiSilicon private ioctls, monitor iface -95) | vendor binary, not our driver | not a target: use hostapd-mini |
| 6 | cipher suites | already advertised: WEP40/WEP104/TKIP/CCMP/GCMP/AES_CMAC/BIP_CMAC_256 (wiphy->cipher_suites) | no change needed |
| 7 | interface combinations for AP | not needed by cfg80211 for a single AP: cfg80211_iftype_allowed(..., check_swif=0) checks wiphy->interface_modes, which already has AP|STATION|MONITOR; NEW_INTERFACE for type __ap is accepted | documented, no combinations added (they would only constrain) |

## 2. Module v2 (built by this lane, host-only)

    artifact : /tmp/wifi-ap-start-bench-artifact/hi5622_wifi.ko
    size     : 206,708 B
    sha256   : fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020
    vermagic : 6.18.60 SMP mod_unload ARMv7 p2v8
    depends  : cfg80211

The artifact is **debug-stripped** (the kernel tree has DEBUG_INFO on, so the
raw .ko embeds the build path and its sha varies with the work directory).
The stripped module is path-independent and keeps .symtab/.rel.* needed for
loading and EXPORT_SYMBOL; build-bench-module.sh prints exactly this size/sha.
    base     : wifi-scan-complete/src (748,888 B b42ff4ec..., hardware-fired
               provider tree; already contains the wifi-vap-start integration)
    content  : base + wifi-ap-start builder/provider patch v3
               (caps AP_START, AP-start sequence in .start_ap, .connect gate,
                corrected SET_INTERFACE, ap_start_mode param)
    evidence : H2D/core/provider files byte-identical to the fired base;
               deployable .text byte-identical to the unstripped build

Reproduce: [../tools/build-bench-module.sh](tools/build-bench-module.sh)
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

Config: [../recipes/hostapd-open.conf](recipes/hostapd-open.conf)
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
| "Driver does not support authentication/association or connect commands" | module without the .connect gate (old .ko) is loaded | verify sha256 24e0cf02..., insmod the v3 module |
| "Could not configure driver mode" | wlan0 is managed and change_virtual_intf refused (old module) OR interface busy | use the v3 module (its SET_INTERFACE op now works); or create wlan0 with type __ap directly |
| ".start_ap ... -EOPNOTSUPP" | module has no AP_START provider (old .ko) or ap_start_mode invalid | verify v3 module; ap_start_mode must be 0 or 1 |
| h2d txq_sync72 announce timeout / H2D lane closed | v2 was built from the older wifi-scan-ap tree | v3 is built from the hardware-fired wifi-scan-complete base; its H2D/core files are byte-identical to the fired base (see section 8) |
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
  supported first fire; [../recipes/hostapd-wpa2.conf](recipes/hostapd-wpa2.conf)
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
## 8. v3 rebuild (second fire, 2026-11-26)

Two findings from the v2 fire, both fixed in v3:

### 8.1 C2 txq_sync72 timeout -> v2 was built from the wrong tree

v2 was built from docs/incoming/wifi-scan-ap/src; the hardware-fired provider
base is docs/incoming/wifi-scan-complete/src (module 748,888 B sha256
b42ff4ec..., D2 TRUE PASS).  v3 builds from the fired base and the build
script now:

* deletes all generated build artifacts before compiling (v2's tree carried
  stale hi5622_wifi.mod/.mod.c/Module.symvers and older scan/provider objects);
* asserts h2d_send.c/h2d_send_logic.c/h2d_send.h/h2d_send_logic.h/hcc.c/
  hcc_core.c/hcc_core.h/h2d_frames.c/h2d_frames.h are byte-identical to the
  fired base -> prints "H2D/core/provider files byte-identical to the
  748,888 B fired base";
* strips only debug sections and asserts the deployable .ko has a
  byte-identical .text to the unstripped build.

v3 module: **206,708 B sha256
fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020**
(/tmp/wifi-ap-start-bench-artifact/hi5622_wifi.ko, debug-stripped, depends=
cfg80211, vermagic 6.18.60 SMP ARMv7, zero warnings).  The H2D send path in
v3 is therefore exactly the path that consumes C2 in 0 polls on the bench.
If the timeout ever recurs with v3, capture h2d_status (frames_sent/budget)
and out[] before doing anything else - do not retry in the same boot.

### 8.2 SET_INTERFACE ("Could not configure driver mode")

The M1 change_virtual_intf op had two bugs, both fixed in v3:

| bug | fix |
| --- | --- |
| returned 0 for a type change without storing wdev->iftype (cfg80211_change_iface WARNs and later AP checks read the stale type) | set dev->ieee80211_ptr->iftype = type (and ARPHRD_IEEE80211_RADIOTAP for monitor / back to ARPHRD_ETHER when leaving monitor) |
| compared params->use_4addr (-1 = "not specified" from nl80211_set_interface) against wdev->use_4addr and refused | only an explicit 4-address request is refused; -1 is ignored |

Same-type SET_INTERFACE does not even reach the op: nl80211_set_interface only
calls cfg80211_change_iface when the type/4addr/monitor parameters actually
change, so hostapd's set_mode(AP) on an interface already created as __ap is a
no-op success.  The station->AP transition is the path the op now handles.

**Test status:** not hardware-tested (this lane is host-only); the code path is
derived from 6.18.60 net/wireless/nl80211.c (change flag + use_4addr = -1) and
net/wireless/util.c (cfg80211_change_iface WARN requiring the driver to store
iftype).  The v3 bench fire is the deciding test: on success hostapd must print
"nl80211: Setup of wlan0 completed" / "wlan0: AP-ENABLED" with no
"Could not configure driver mode".

### 8.3 Re-fire command (updated module)

    tools/rscp.sh /tmp/wifi-ap-start-bench-artifact/hi5622_wifi.ko root@192.168.1.1:/tmp/
    # fresh cold boot; golden page; then:
    insmod /tmp/cfg80211.ko
    insmod /tmp/hi5622_wifi.ko allow_frames=8 frame_gap_ms=100 h2d_send_budget=8
    # boot prep + C2 ladder until the post gate reports ACTIVE
    iw reg set US
    iw dev wlan0 del 2>/dev/null || true
    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up
    /usr/sbin/hostapd -dd -t /tmp/hostapd-open.conf 2>&1 | tee /tmp/hostapd.log

Readback unchanged: ctx0 BAR0 0x7D1758 -> ctx0+0x18C (0x7D18E4) changes after
ap_cfg; VAP = u32 at 0x7D18E8, then V+0x6B8000+0x10 = 100 and +0x14 = 2.

## 9. v4 (second fire follow-up): the missing .mgmt_tx capability

The lead's isolation showed the SET_INTERFACE op itself works (manual
`iw dev wlan0 set type managed` and `set type __ap` both returned 0
with the driver dmesg lines), yet hostapd printed "Could not configure driver
mode" with no type-change message.  Static analysis of the hostapd 2.12
snapshot and the 6.18.60 kernel found the exact gate:

1. hostapd's `wpa_driver_nl80211_finish_drv_init()` calls
   `wpa_driver_nl80211_set_mode(bss, NL80211_IFTYPE_AP)`; the SET_INTERFACE
   succeeds (no-op if the iface is already AP -> no dmesg, which is exactly
   what the fire showed).
2. `wpa_driver_nl80211_set_mode_impl()` then, for AP mode, calls
   `nl80211_setup_ap()` -> `nl80211_mgmt_subscribe_ap()`, which sends
   `NL80211_CMD_REGISTER_FRAME` for auth/assoc/reassoc/disassoc/deauth/
   probe-request (and registers the action/class-3 handlers).
3. The kernel's `nl80211_register_mgmt()` refuses every REGISTER_FRAME
   request when the driver has no `.mgmt_tx` op:
   `/* not much point in registering if we can't reply */ if (!rdev->ops->mgmt_tx) return -EOPNOTSUPP;`
   (net/wireless/nl80211.c).  The clean-room driver advertised no .mgmt_tx,
   so the subscription failed, setup_ap returned -1 and hostapd reported
   "Could not configure driver mode" - after the mode change had already
   succeeded.

**Fix (v4):** register a fail-closed `.mgmt_tx` op.  cfg80211 then accepts
the management-frame subscriptions; the op itself returns -EOPNOTSUPP (real
management TX/RX is data-plane/M4 work, never faked).

**Module v7:** 206,708 B sha256
fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020 (debug-stripped, same fired base; H2D/core files
byte-identical, .text byte-identical stripped/unstripped).  This is the
artifact to fire.

Expected progression after this fix (same command sequence as section 8.3):
hostapd proceeds past set_mode, prints `nl80211: Setup of wlan0 completed`,
then calls NEW_BEACON -> our `.start_ap` -> dmesg
`start_ap sequence frame 1/4 'ap_ssid' posted (64 B)` /
`2/4 'ap_cfg' posted (184 B)` -> `wlan0: AP-ENABLED`.

If it still stops, the next candidate gates are: (a) NEW_BEACON rejected by
cfg80211 regulatory (NO_IR - run `iw reg set US` first), (b) a beacon
attribute cfg80211 rejects (capture the -dd line), (c) a later hostapd step
needing add_key (WPA2 - use the OPEN config first).  The fail-closed
mgmt_tx means hostapd will not receive auth frames: an AP with clients needs
the data-plane lane, but the control-plane deadline (beacon start) does not.

## 10. v5: the actual AP-setup gate - wiphy->mgmt_stypes

The v4 fire still failed with "Could not configure driver mode" and the
hostapd-mini build has MSG_DEBUG compiled out, so the command was invisible.
v4's .mgmt_tx was necessary but NOT sufficient: cfg80211 has a second check
that runs inside the same REGISTER_FRAME path:

1. `nl80211_register_mgmt()` (passes with .mgmt_tx present) calls
   `cfg80211_mlme_register_mgmt()` (net/wireless/mlme.c).
2. That function first does
   `if (!wdev->wiphy->mgmt_stypes) return -EOPNOTSUPP;` and then
   `if (!(wdev->wiphy->mgmt_stypes[wdev->iftype].rx & BIT(mgmt_type))) return -EINVAL;`
   for every frame type hostapd subscribes to.
3. The clean-room driver never set `wiphy->mgmt_stypes`, so the first
   CONNECT/AUTH/ASSOC_REQ/... subscription returned -EOPNOTSUPP, setup_ap
   failed, and hostapd printed the mode error right after a successful
   SET_INTERFACE (hence no type-change dmesg).

**Fix (v5):** advertise a per-iftype mgmt_stypes table in the wiphy:
AP rx/tx = ASSOC_REQ, REASSOC_REQ, PROBE_REQ, DISASSOC, AUTH, DEAUTH, ACTION;
STA rx = (re)assoc resp, probe resp, disassoc/deauth, action; monitor rx/tx
all subtypes.  The management TX path stays fail-closed (data-plane/M4 work);
the table only lets cfg80211 accept hostapd's subscriptions.

**Visibility for the next fire (also in v5):** the module now logs every
bring-up milestone via dmesg (no hostapd debug needed):

| dmesg line | meaning |
| --- | --- |
| cfg80211: mgmt_stypes advertised (AP rx=0x.... tx=0x....) - hostapd frame subscriptions accepted | module init; confirms the v5 capability set |
| cfg80211: SET_INTERFACE reached (iftype X -> Y, use_4addr=Z) | hostapd's nl80211_set_mode reached the driver op |
| cfg80211: start_ap reached (iftype=.. beacon_interval=.. dtim=.. head=.. tail=..) | hostapd's NEW_BEACON reached .start_ap |
| start_ap sequence frame 1/4 'ap_ssid' posted ... / 2/4 'ap_cfg' posted ... | provider frames went to the wire |
| pr_info_once "mgmt_tx op reached" | only if hostapd tries a management TX (fails closed) |

**Module v7:** 206,708 B sha256
fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020
(debug-stripped, fired base; H2D/core files byte-identical; .text identical
stripped/unstripped; 0 warnings).

Re-fire with the same command sequence as section 8.3 (swap in the v5 .ko).
Readback unchanged: ctx0 BAR0 0x7D1758, ctx0+0x18C (0x7D18E4) changes after
ap_cfg; VAP=u32@0x7D18E8, V+0x6B8000+0x10=100, +0x14=2.

Remaining candidates if hostapd still stops after v5 (each is now visible in
dmesg):
* (b) regulatory: NEW_BEACON's cfg80211_reg_can_beacon_relax can reject a
  NO_IR channel.  `iw reg get` on the box must show a real country (US) and
  the 2.4 GHz channel flags must not be no-IR; `iw reg set US` before
  hostapd.  If it is no-IR, the "start_ap reached" log still appears but the
  cfg80211 return code is -EINVAL.
* (c) later hostapd steps (add_key for WPA2 - use the OPEN config first;
  set_txq_params is non-fatal and skipped because the driver op is absent).

## 11. v6: the actual beacon blocker (REPORTS_OBSS) + WARN + SET_BEACON

### 11.1 It was never SET_BEACON

hostapd's first beacon uses **NL80211_CMD_NEW_BEACON**: in
`wpa_driver_nl80211_set_ap()`, `beacon_set = params->reenable ? 0 : link->beacon_set`
is 0 at start, and only a non-zero value selects NL80211_CMD_SET_BEACON.
cfg80211's SET_BEACON handler is additionally gated on
`rdev->ops->change_beacon` AND a non-zero
`wdev->links[].ap.beacon_interval` (set only by a successful
start_ap), so **SET_BEACON cannot be the initial failure**.  v6 nonetheless
registers a `.change_beacon` op for later updates: it accepts host-side,
logs the beacon lengths and performs **no firmware write yet** (the real
firmware beacon update is g0 id1 key 0x0127 with start flag 0; wiring it
needs cached AP state - M3).

### 11.2 The real blocker: WIPHY_FLAG_REPORTS_OBSS

With userspace AP SME (`drv->device_ap_sme == 0`) and
`beacon_set == 0`, `wpa_driver_nl80211_set_ap()` first calls
`nl80211_get_wiphy_data_ap(bss)`; if that returns NULL the function
returns **-ENOBUFS before building the NEW_BEACON message** (so cfg80211 never
sees it, our .start_ap never runs, and no dmesg appears).  That helper
registers beacon events with `NL80211_CMD_REGISTER_BEACONS`, and
cfg80211's `nl80211_register_beacons()` rejects it with -EOPNOTSUPP
unless the wiphy advertises:

    if (!(rdev->wiphy.flags & WIPHY_FLAG_REPORTS_OBSS))
            return -EOPNOTSUPP;      /* net/wireless/nl80211.c */

The clean-room wiphy never set that flag.  **Fix (v6):** set
`WIPHY_FLAG_REPORTS_OBSS`.  The driver has no OBSS beacon reporting
implementation, so the flag only satisfies hostapd's registration
bookkeeping; no beacon events are expected from the firmware path.

### 11.3 The kernel warning: connect without disconnect

core.c:532 is

    WARN_ON(ops->connect && !ops->disconnect);

v2 added the fail-closed `.connect` gate for hostapd's driver-init check
without a paired `.disconnect`, hence the wiphy_new_nm+0x5b8 WARN.
**Fix (v6):** register a fail-closed `.disconnect` stub (never used by the
AP path).

### 11.4 The mgmt_tx caller

v6 adds `dump_stack()` to the one-shot mgmt_tx diagnostic so the next fire
prints the caller in dmesg.  The op still fails closed; the current failure
does not depend on it (the beacon path died before NEW_BEACON).

### 11.5 Module v6 and expected dmesg

**Module v7:** 206,708 B sha256
fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020 (debug-stripped, fired base; H2D/core files
byte-identical; .text identical stripped/unstripped; 0 warnings).

Expected order for the next fire (same commands as section 8.3):

    cfg80211: REPORTS_OBSS flag set (hostapd wiphy-data AP registration)   [module init]
    cfg80211: mgmt_stypes advertised (AP rx=0x3c15 tx=0x3c15) ...           [module init]
    cfg80211: SET_INTERFACE reached ...                                     [hostapd set_mode]
    cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 ...)    [NEW_BEACON]
    cfg80211: start_ap sequence frame 1/4 'ap_ssid' posted (64 B ...)
    cfg80211: start_ap sequence frame 2/4 'ap_cfg' posted (184 B ...)
    cfg80211: start_ap sequence posted to provider '...' (2 frame(s))
    hostapd: wlan0: AP-ENABLED

Readback unchanged: ctx0 BAR0 0x7D1758; ctx0+0x18C (0x7D18E4) changes after
ap_cfg; VAP = u32 at 0x7D18E8; V+0x6B8000+0x10 = 100, +0x14 = 2.

## 12. v7: the exception was a missing VAP object + the mislabelled ap_cfg

### 12.1 What frame 2 actually was

The v6 fire's dmesg label "frame 2/4 'ap_auth'" was **wrong**: the g0 carrier
has only two members (ap_ssid, ap_cfg) and the label array was index-based, so
index 1 printed "ap_auth" while the provider actually built **ap_cfg** (g0 id1
key 0x0127).  Corrected spec of the frame that was posted:

| field | value |
| --- | --- |
| carrier | g0 id1 config event |
| total | **184 B** = 24 B header/key/len + 156 B body + 4 B inherited 0xFF pad |
| key | **0x0127** (firmware table-A row 110 -> worker 0x84DA4) |
| body | 156 B, beacon_interval=100 at +0x00, dtim=2 at +0x04, mode/etc. (TABLES T3) |

There is NO auth frame on the g0 carrier: firmware table A has **no 0x006D
row**.  The 36-B WAL auth frame (group-2 cmd 0x006D, 4-B body) exists only in
the mode-1/ vendor order [0x0005 -> 0x006D -> 0x0127 -> 0x0065], and g0
deliberately omits it (open AP: no auth algorithm needed; WPA2 rides the RSN
IE).  v7 makes the labels carrier-aware through a provider callback, so dmesg
now prints ap_cfg for the 184-B frame.

### 12.2 Root cause of the firmware exception

Both AP workers dereference the **firmware VAP object at module_ctx+0x190
without a NULL check**:

* set-SSID worker CA 0x82878: `ldr.w r5,[r0,#0x190]; adds r5,#0x24; memcpy_s`
* AP-config worker CA 0x84DA4: `ldr r3,[r4,#0x190]; ldrb r2,[r3,#0x64]` and
  `str r3,[r2,#0x10]/[r2,#0x14]` for beacon_interval/DTIM

The AP flow never posted the **mode-1 VAP base** (g0 id1 key 0x0064/0x0065):
that base was only posted by the scan path (`vap_start_first`).  With no
VAP object, the first AP frame's worker took a firmware data abort ->
exception mask 0x18, golden page lost, staging 0xffffffff, and the second
frame's announce was never consumed.  (The SSID frame may well have been the
faulting one - announce consumption is at the SR/queue level, not proof the
worker ran.)

**Fix (v7):** `.start_ap` now posts the mode-1 VAP base
(g0 id1 key 0x0064 48 B -> key 0x0065 40 B) **always, once per module load**,
before any AP frame (flag-guarded; the scan path keeps the vap_start_first
gate).  If the VAP base cannot be posted the sequence aborts before the wire
(fail-closed).

### 12.3 Recovery / next fire

The exception state cannot be repaired host-side: **power-cycle** (the lead is
rebooting).  A fresh golden boot is mandatory before the next fire; never
reuse the exception boot.

Module v7: **206,708 B sha256
fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020** (debug-stripped, fired base 748,888 B b42ff4ec;
H2D/core files byte-identical; .text identical stripped/unstripped; 0
warnings).

Expected dmesg (same command sequence as section 8.3; `allow_frames=8` is
enough - the AP path now posts 2 VAP-base frames + 2 AP frames):

    cfg80211: REPORTS_OBSS flag set ...
    cfg80211: mgmt_stypes advertised (AP rx=0x3c15 tx=0x3c15) ...
    cfg80211: SET_INTERFACE reached ...
    frame post vap_add: g0 id1 total 48 B ... posted 1/8        [mode-1 VAP base]
    frame post vap_start: g0 id1 total 40 B ... posted 2/8
    cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=63 tail=9) ...
    frame post ap_ssid: g0 id1 total 64 B ... posted 3/8        [key 0x0005]
    cfg80211: start_ap sequence frame 1/4 'ap_ssid' posted (64 B ...)
    frame post ap_cfg: g0 id1 total 184 B ... posted 4/8        [key 0x0127]
    cfg80211: start_ap sequence frame 2/4 'ap_cfg' posted (184 B ...)
    cfg80211: start_ap sequence posted to provider '...' (2 frame(s))
    hostapd: wlan0: AP-ENABLED

Readback: VAP pointer ctx0+0x190 (BAR0 0x7D18E8) nonzero after the VAP base;
after ap_cfg: VAP+0x10 (u32) = 100 and VAP+0x14 = 2 (BAR0 = VAP_CA+0x6B8000);
ctx0+0x18C (0x7D18E4) changes; mask 0, staging 0, golden page restored after
the power-cycle.

## 12. v7: the exception was a missing VAP object + the mislabelled ap_cfg

### 12.1 What frame 2 actually was

The v6 dmesg label "frame 2/4 'ap_auth'" was **wrong**: the g0 carrier has
only two members (ap_ssid, ap_cfg) and the label array was index-based, so
index 1 printed "ap_auth" while the provider actually built **ap_cfg** (g0 id1
key 0x0127).  Corrected spec of the frame that was posted:

| field | value |
| --- | --- |
| carrier | g0 id1 config event |
| total | **184 B** = 24 B header/key/len + 156 B body + 4 B inherited 0xFF pad |
| key | **0x0127** (firmware table-A row 110 -> worker 0x84DA4) |
| body | 156 B: beacon_interval=100 at +0x00, dtim=2 at +0x04, mode/etc. (TABLES T3) |

There is NO auth frame on the g0 carrier: firmware table A has **no 0x006D
row**.  The 36-B WAL auth frame (group-2 cmd 0x006D, 4-B body) exists only in
the vendor order [0x0005 -> 0x006D -> 0x0127 -> 0x0065], and g0 deliberately
omits it (open AP: no auth algorithm needed; WPA2 rides the RSN IE).  v7 makes
the labels carrier-aware through a provider callback, so dmesg now prints
ap_cfg for the 184-B frame.

### 12.2 Root cause of the firmware exception

Both AP workers dereference the **firmware VAP object at module_ctx+0x190
without a NULL check**:

* set-SSID worker CA 0x82878: `ldr.w r5,[r0,#0x190]; adds r5,#0x24; memcpy_s`
* AP-config worker CA 0x84DA4: `ldr r3,[r4,#0x190]; ldrb r2,[r3,#0x64]` and
  `str r3,[r2,#0x10]/[r2,#0x14]` for beacon_interval/DTIM

The AP flow never posted the **mode-1 VAP base** (g0 id1 key 0x0064/0x0065):
that base was only posted by the scan path (`vap_start_first`).  With no VAP
object, the first AP frame's worker took a firmware data abort -> exception
mask 0x18, golden page lost, staging 0xffffffff, and the second frame's
announce was never consumed.  (The SSID frame may well have been the faulting
one - announce consumption is at the SR/queue level, not proof the worker
ran.)

**Fix (v7):** `.start_ap` now posts the mode-1 VAP base (g0 id1 key 0x0064
48 B -> key 0x0065 40 B) **always, once per module load**, before any AP frame
(flag-guarded; the scan path keeps the vap_start_first gate).  If the VAP base
cannot be posted the sequence aborts before the wire (fail-closed).

### 12.3 Recovery / next fire

The exception state cannot be repaired host-side: **power-cycle** (the lead is
rebooting).  A fresh golden boot is mandatory before the next fire; never
reuse the exception boot.

Module v7: **206,708 B sha256
`fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020`** (debug-stripped, fired base 748,888 B b42ff4ec; H2D/core files
byte-identical; .text identical stripped/unstripped; 0 warnings).

Expected dmesg (same command sequence as section 8.3; `allow_frames=8` is
enough - the AP path now posts 2 VAP-base frames + 2 AP frames):

    cfg80211: REPORTS_OBSS flag set ...
    cfg80211: mgmt_stypes advertised (AP rx=0x3c15 tx=0x3c15) ...
    cfg80211: SET_INTERFACE reached ...
    frame post vap_add: g0 id1 total 48 B ... posted 1/8        [mode-1 VAP base]
    frame post vap_start: g0 id1 total 40 B ... posted 2/8
    cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=63 tail=9) ...
    frame post ap_ssid: g0 id1 total 64 B ... posted 3/8        [key 0x0005]
    cfg80211: start_ap sequence frame 1/4 'ap_ssid' posted (64 B ...)
    frame post ap_cfg: g0 id1 total 184 B ... posted 4/8        [key 0x0127]
    cfg80211: start_ap sequence frame 2/4 'ap_cfg' posted (184 B ...)
    cfg80211: start_ap sequence posted to provider '...' (2 frame(s))
    hostapd: wlan0: AP-ENABLED

Readback: VAP pointer ctx0+0x190 (BAR0 0x7D18E8) nonzero after the VAP base;
after ap_cfg: VAP+0x10 (u32) = 100 and VAP+0x14 = 2 (BAR0 = VAP_CA+0x6B8000);
ctx0+0x18C (0x7D18E4) changes; mask 0, staging 0, golden page restored after
the power-cycle.
