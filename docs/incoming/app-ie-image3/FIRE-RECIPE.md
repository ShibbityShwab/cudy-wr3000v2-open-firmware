# FIRE-RECIPE - the H2D g0 id 0x20 app/vendor IE injection on the IMAGE #3 module lineage

**Lane:** app-ie-rebase (task-235). **This lane never fires.** The Lead fires
with the resident bench procedure: bench link PASS, recovery card READY, human
at the bench, UART capture read-only. One change per fire, one rung per fresh
boot, no in-place retry, one load. Inherit **all** non-negotiables from
`../wifi-ax-he-caps/FIRE-RECIPE.md` and `../wifi-5ghz-beacon/FIRE-RECIPE.md`:
recovery armed, UART read-only (never write to the tty), never write CA
0x400392f0, never read the RC misc window 0x10161000..0x1016ffff, never rmmod
after the CPU release, never touch endpoint 0001:00:00.0, page/state/mask
anomaly -> power-cycle.

**Module under test:** the host-built `hi5622_wifi.ko` of BUILD-NOTES.md -
the **IMAGE #3 merged module tree** (a717c86d source) + `patches/0001-app-ie-image3.patch`,
installed form (`strip-kmod.sh NO_RENAME=1`):
**364,516 B sha256 `2a2fff3508bdbffad4a23d04c31c17ef91f7ce46ae190fdae090119bcb6263e2`**,
**66 params** (the IMAGE #3 64 + `app_ie`, `app_ie_type`), `.text` sha256
`c1ad137974b6866bbc7cdc1971288ade203bf9dfcca6421b73343ef3d50b9417`.
If the deployed file does not match, STOP (wrong build). The image's own
module `a717c86d` (358,544 B, 64 params) is the accepted IMAGE #3 module; this
rung replaces it on a fresh boot so the only delta is the app-IE integration
(and with `app_ie=0` the module posts no new frame).

**The question this fire answers:** does the cfg80211 AP-start vendor IE
(hostapd `vendor_elements=dd...`) reach the firmware's per-VAP IE class slot
via the H2D g0 id 0x20 frame (the read-only slot read-back line), and does it
appear on air in the AP's beacon / probe response (the laptop scan)? Host-side
PASS is the posted line + the read-back; the on-air half needs the laptop
observer.

**Placement note (the rebase delta):** the IMAGE #3 ladder has one VAP-start
step (`ap_vap_start`); the 0x20 frame is posted **after** it, before
`start_ap()` commits (`REPORT.md` sec. 3). The read-back is diagnostic; the
laptop scan is decisive. The class -> frame mapping stays [I]: class 4 is the
stock-observed class, first rung.

---

## H0 - host preconditions (read-only)

    uname -r                                    # 6.18.60
    sha256sum /lib/modules/6.18.60/hi5622_wifi.ko
        # expect 2a2fff3508bdbffad4a23d04c31c17ef91f7ce46ae190fdae090119bcb6263e2
    stat -c %s /lib/modules/6.18.60/hi5622_wifi.ko   # 364516
    modinfo /lib/modules/6.18.60/hi5622_wifi.ko | grep -c '^parm:'   # 66
    modinfo -p /lib/modules/6.18.60/hi5622_wifi.ko | grep -E '^app_ie'
        # app_ie: (uint)   app_ie_type: (uint)
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin /lib/firmware/hi_wifi/wifi_block.bin
    cat /etc/hi5622/FIRMWARE-POLICY.txt          # firmware is never packaged

Golden-state gate (read-only, the IMAGE #3 F2/HE rung):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status          # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/size/param count differs (wrong build) or the chip state
is not healthy. A fresh power-cycle before the load is required (the image
boots with `hi5622.main.autoload=0`, so no module is loaded; if
`/proc/modules` lists `hi5622_wifi`, the load-time parameters cannot be
changed - reboot, never rmmod).

## H1 - load with app_ie=1 app_ie_type=4 (IMAGE #3 accepted 2.4 GHz profile, ONE load, no retry)

Push the rebuilt module first (attended; local artifact, never committed):

    scp /tmp/app-ie-image3/hi5622_wifi.ko root@192.168.10.1:/lib/modules/6.18.60/hi5622_wifi.ko

The shipped `/usr/sbin/hi5622-load` cannot pass the two new parameters (it is
the fixed IMAGE #3 loader), so load by explicit insmod - the same option set
the accepted IMAGE #3 2.4 GHz rung used, with `he_caps=1` and the app-IE
knobs added:

    insmod /lib/modules/6.18.60/cfg80211.ko       # if not already loaded
    insmod /lib/modules/6.18.60/hi5622_wifi.ko         fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin         allow_frames=32 frame_gap_ms=100 h2d_send_budget=10         vap_start_first=1 vap_start_mode=1 vap_start_module=0 scan_dwell_ms=60         hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1         rx_refill_wm=75 beacon_static=1 beacon_vap_start=1 wpa_keys=1         wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0         scan_band_cfg=0 band5g=0 vap5g=0 ap_chan_wait_ms=300 desc_table=0         dfs_mode=0 he_caps=1 ba_enable=0 mgmt_tx=0 mgmt_rx=0 sta_ops=0         eapol_rx=0 sta_user_del=0 data_tx=0 data_tx_peer=1 data_tx_protect=0         data_tx_timeout_ms=200 txq_publish=1 cfg80211_auto=1 ap_start_mode=0         rd_ab_write=0 hcc_cece_mode=0 allow_unseeded_release=0         app_ie=1 app_ie_type=4

Verify the knobs (all; the last two are this lane's):

    for p in beacon_static beacon_vap_start he_caps app_ie app_ie_type; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 1 1 1 1 4

STOP if any value differs. `app_ie=1` needs `beacon_static=1` and a posted
VAP start (`beacon_vap_start=1` here); the gate refuses before any frame
otherwise. `he_caps=1` is the accepted IMAGE #3 HE AP state; the app-IE frame
is independent of it. `desc_table=0`/`dfs_mode=0`/`band5g=0` keep the 5 GHz
chain fail-closed (this is a 2.4 GHz rung).

## H2 - boot ladder / C2 (read-only evidence)

Wait for the wiphy (the module owns the ready handshake), then run the C2
ladder exactly as the IMAGE #3 loader does:

    for i in $(seq 1 30); do [ -e /sys/class/ieee80211/phy0 ] && break; sleep 1; done
    ls /sys/class/ieee80211/                   # exactly phy0
    D=/sys/bus/pci/devices/0000:00:00.0
    echo 3 > $D/h2d_ctx_create
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status                          # expect ok=2 timeout=0 refused=0
    cat $D/boot $D/status                      # page 0x86807FEF / state 1 / mask 0

## H3 - 2.4 GHz AP with a hostapd vendor element (the accepted IMAGE #3 fixture + ONE line)

The accepted IMAGE #3 2.4 GHz AP is the WPA2/WPA3-SAE fixture (SSID
`cudy-wr3000v2-wpa23`, proven AP-ENABLED on the image). Copy it and append the
vendor element (element id 0xDD, len 0x0A, OUI 00:11:22, type 0x33, ASCII
`appie!`):

    cp /etc/hi5622/hostapd-wpa2-wpa3.conf /tmp/hostapd-2g-appie.conf
    printf '\nvendor_elements=dd0a00112233617070696521\n' >> /tmp/hostapd-2g-appie.conf
    grep vendor_elements /tmp/hostapd-2g-appie.conf
    iw reg set US
    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up
    /usr/sbin/hostapd -B -t -P /var/run/hi5622-hostapd.pid /tmp/hostapd-2g-appie.conf
    logread -f | grep -E 'cfg80211|app_ie'        # or dmesg -w

Alternative proven 2.4 GHz fixture (open HE AP): `/etc/hi5622/hostapd-open-ax.conf`
+ the same one line. The vendor element is independent of the security mode.

**Expected dmesg, in this order (all of):**

    cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=... tail=...)
    cfg80211: app_ie: collected NN B of vendor IE(s) (0xDD) from the cfg80211 AP settings (1 element(s), type class 4) - the g0 0x20 step runs after the image #3 'ap_vap_start' step
    cfg80211: start_ap beacon_static=1 plan (6 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start app_ie
    cfg80211: start_ap sequence frame ... 'ap_ssid' / 'ap_auth'? / 'ap_cfg' posted ...
    cfg80211: start_ap beacon_static: 'ap_vap_start' posted after ap_cfg (channel=6) - vendor AP-start order complete
    cfg80211: start_ap beacon_static: 'app_ie' posted (35 B, g0 id 0x20, payload ie_len=12 type=4 mode=0, 1 vendor IE(s), VAP/module=0) - last step, after the image #3 'ap_vap_start' VAP start
    cfg80211: app_ie post: mctx=0x... vap=0x... slot(class 4)=vap+0x2f0 ptr=0x... len=12 cap=... (expect stored len=12; firmware slot MATCHES the posted IE list)
    wlan0: AP-ENABLED

(35 B = 23 + the 12-byte vendor IE TLV `dd0a00112233617070696521`; hostapd
appends the element to both the beacon buffer and the probe-response IE buffer,
so the reported `ie_len`/frame size can be 12/24 B - the posted line reports
the real collected length. If the fixture carries other 0xDD elements too,
"1 element(s)" becomes the real count.)

PASS (host side, all of):
1. the plan line ends with `app_ie` (after `ap_vap_start`, the last step);
2. the `'app_ie' posted` line reports g0 id 0x20, payload ie_len == the
   collected length, type 4, mode 0, VAP/module=0 and >= 1 vendor IE;
3. the `app_ie post` read-back resolves the VAP and reports the class-4 slot
   with `len == ie_len` (`MATCHES`) - diagnostic, non-gating for on-air; a
   "VAP context unresolved" line is a soft fail to record, not a crash;
4. `cat $D/cfg80211_status | grep '^app_ie:'` shows
   `param=1 (ap-post) type=4 ... last_rc=0 posted=1 refused=0`;
5. page 0x86807FEF, state 1, mask 0; no firmware exception; hostapd AP-ENABLED.

## H4 - the on-air check: the laptop scan (decisive for the IE itself)

From the **laptop observer** (its Wi-Fi adapter, not through the router):

    iw dev <laptop-if> scan | grep -i -A2 '00:11:22'
    # or on air: tcpdump -i <laptop-if> -s 256 -vv \
    #     'wlan type mgt subtype beacon or wlan type mgt subtype proberesp'
    #     and look for the element: dd 0a 00 11 22 33 61 70 70 69 65 21

PASS iff the `dd0a00112233617070696521` element is present in the AP's
**probe response** and/or **beacon** (class 4 first; the vendor appends
`vendor_elements` to both buffers).

If the laptop sees the AP but **no 0xDD element**: the host-side frame was
accepted (read-back MATCHES) but the firmware does not append class 4 to the
frames this AP sends, or the after-VAP-start placement is too late for the
template the firmware builds. Record the read-back line + the class-4 result,
then do H5 on fresh-boot fires (one class per boot).

## H5 - class sweep (only if H4 saw no element; one class per fire)

Reload with `app_ie_type=` 9, then 0, 1, 2, 3, 5, 6, 7 (one fresh boot each;
class 4 already tested). The read-back line prints the slot address for the
chosen class (`slot(class N)=vap+0x...`), so the sweep stays cheap. PASS on
the first class whose element the laptop sees. Classes 12/17/18 are path-B
classes and are deliberately **not** supported by this build.

## H6 - negative controls (separate fires, one load per boot)

    # app_ie=0 control: no app_ie plan step, no 0x20 frame, laptop sees no
    # 0xDD element; hostapd still AP-ENABLED (proves the frame is the only change)
    insmod ... app_ie=0 app_ie_type=4 ...
    # app_ie=1 but the hostapd config has no vendor_elements= line: dmesg says
    # "app_ie=1 but the AP settings carry no vendor IE ... step is skipped",
    # no 0x20 frame, AP-ENABLED (proves the frame is IE-driven)
    # bad parameters are refused BEFORE any frame (start_ap fails, no AP):
    insmod ... app_ie=2 ...                 # REFUSED: app_ie not 0/1
    insmod ... app_ie=1 app_ie_type=20 ...  # REFUSED: class > 19
    # app_ie=1 without beacon_static=1: REFUSED (no ladder slot)

STOP: if a start_ap is refused after some frames reached the wire, the AP
start latches DIRTY (the module says so in dmesg) - power-cycle, never retry
in place. A new module parameter needs a reboot (no rmmod after a CPU release).
