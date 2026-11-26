# FIRE-RECIPE - the H2D g0 id 0x20 app/vendor IE injection on the 2.4 GHz HE/Wi-Fi6 AP

**Lane:** wifi-app-ie (task-234).  **This lane never fires.**  The Lead fires
with the resident bench procedure (bench link PASS, recovery card READY, human
at the bench, UART capture read-only).  One change per fire, one rung per fresh
boot, no in-place retry, one load.

**Module under test:** the host-built `hi5622_wifi.ko` of BUILD-NOTES.md /
`evidence/BUILD.txt` - the R5-order base + `patches/0001-app-ie.patch`,
installed form (`strip-kmod.sh NO_RENAME=1`): **368,348 B** sha256
`0c8390e8b498b668a066f4e7c74ad9fdafa7aa5586db22fb81313409cfcb0bdb`,
**67 params** (65 base + `app_ie`, `app_ie_type`), `.text` sha256
`7867d4f0294bae925c9118a4e4360fd1b4815f6d57c9abea8318a9e1caa9a207`.
If the deployed file does not match, STOP (wrong build).  Preferred cadence:
integrate the patch into the *current* image-#3/TWT module first
(`tools/build-app-ie.sh` with `TREE_SRC` pointing at that tree re-derives the
pins; the `.text` hash is the stable identity) and fire that - then this recipe
is one variable on the accepted image.

**The question this fire answers:** does a cfg80211 AP-start vendor IE
(hostapd `vendor_elements=dd...`) reach the firmware's per-VAP IE class slot
via the g0 id 0x20 frame (the read-back line), and does it appear on air in
the AP's probe response / beacon (the laptop scan)?  Host-side PASS is the
read-back; the on-air half needs the laptop observer.

Inherit **all** non-negotiables from `../wifi-ax-he-caps/FIRE-RECIPE.md` and
`../wifi-5ghz-beacon/FIRE-RECIPE.md`: recovery armed, UART read-only, bench
link PASS, never write CA 0x400392f0, never read the RC misc window
0x10161000..0x1016ffff, never rmmod after the CPU release, never touch
endpoint 0001:00:00.0, page/state/mask anomaly -> power-cycle.

---

## H0 - host preconditions (read-only)

    uname -r                                   # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect 0c8390e8b498b668a066f4e7c74ad9fdafa7aa5586db22fb81313409cfcb0bdb
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko     # 368348
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'   # 67
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -E '^app_ie'
        # app_ie: (uint)   app_ie_type: (uint)
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin

Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status          # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/size/param count differs (wrong build) or the chip
state is not healthy.  A fresh power-cycle before the load is preferred.

## H1 - load with app_ie=1 app_ie_type=4 (2.4 GHz AP profile, ONE load, no retry)

The proven 2.4 GHz AP-start set of this base, plus exactly the app-IE knobs:

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 vap_add_index=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 rx_enable=1 \
        beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0 \
        sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 \
        band5g=0 vap5g=0 ap_chan_wait_ms=0 desc_table=0 dfs_mode=0 \
        band_start=0 band_start_fallback=0 vap_start_g0=1 \
        app_ie=1 app_ie_type=4

Verify the knobs (all; the last three are this lane's):

    for p in band5g vap5g ap_chan_wait_ms beacon_static desc_table dfs_mode \
             band_start band_start_fallback vap_start_g0 app_ie app_ie_type; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 0 0 0 1 0 0 0 0 1 1 4

STOP if any value differs.  `vap_start_g0=1` is required by the app-IE gate
(on 2.4 GHz the 0x0065 step itself is a clean no-post by design - the gate is
about the ladder slot, not about a 5 GHz frame).

## H2 - boot ladder on endpoint 0000 ONLY (proven order, unchanged)

    D=/sys/bus/pci/devices/0000:00:00.0
    echo 1 > $D/prepare
    echo 1 > $D/upload
    echo 1 > $D/verify
    echo 1 > $D/seed
    echo 1 > $D/hcc_arm
    echo 1 > $D/hcc_loop
    echo 1 > $D/release          # 0x5A5A only; NEVER 0xCECE here

Expected: all rc=0; page 0x86807FEF/7/2/0xABCD; ready handshake decoded;
`cfg80211: wiphy registered ... (1 band(s): 2.4GHz ...)`.

## H3 - wiphy/interface/2.4 GHz AP with a hostapd vendor element

    ls /sys/class/ieee80211/                     # exactly phy0
    echo 3 > $D/h2d_ctx_create                   # the proven C2 pair
    echo 1 > $D/h2d_txq_sync72
    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up

Fixture: the proved open 2.4 GHz AP config of the sibling lane
`../wifi-scan-ap/configs/hostapd-open.conf` (no key path needed), plus ONE
line - the vendor element (id 0xDD, len 0x0A, OUI 00:11:22, type 0x33, ASCII
`appie!`; hex below):

    cp ../wifi-scan-ap/configs/hostapd-open.conf /tmp/hostapd-2g-appie.conf
    printf '\nvendor_elements=dd0a00112233617070696521\n' \
        >> /tmp/hostapd-2g-appie.conf
    grep vendor_elements /tmp/hostapd-2g-appie.conf
    /usr/sbin/hostapd -dd -t /tmp/hostapd-2g-appie.conf 2>&1 \
        | tee /tmp/hostapd-2g-appie.log

(WPA2/WPA3 alternative: the image fixture
`../wifi-image-polish/rootfs/etc/hi5622/hostapd-wpa2-wpa3.conf` + the same
one line.  The vendor element is independent of the security mode.)

**Expected dmesg, in this order (all of):**

```
cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=... tail=...)
cfg80211: app_ie: collected NN B of vendor IE(s) (0xDD) from the cfg80211 AP settings (1 element(s), type class 4) - the g0 0x20 step runs after the 0x0065 VAP start
cfg80211: start_ap beacon_static=1 plan (N steps): vap_add ap_chan ap_ssid ap_cfg vap_start_g0 app_ie
...
cfg80211: start_ap beacon_static frame K/N 'app_ie' posted (35 B, g0 id 0x20, payload ie_len=12 type=4 mode=0, 1 vendor IE(s), VAP/module=0) - after the 0x0065 VAP start, before the 0x0242 band start
cfg80211: app_ie post: mctx=0x... vap=0x... slot(class 4)=vap+0x2f0 ptr=0x... len=12 cap=... (expect stored len=12; firmware slot MATCHES the posted IE list)
```

(35 B = 23 + the 12-byte vendor IE TLV `dd0a00112233617070696521`; hostapd
appends the element to both the beacon buffer and the probe-response IE
buffer, so the collected/reported `ie_len` can be 12 or 24 B - the posted
line reports the real length.)

and hostapd prints **`wlan0: AP-ENABLED`**.

PASS (host side, all of):
1. the plan line lists `app_ie` **between `vap_start_g0` and `band_start`**;
2. the `'app_ie' posted` line reports g0 id 0x20, payload ie_len == the
   collected length, type 4, mode 0, the VAP/module index, and >= 1 vendor IE;
3. the `app_ie post` read-back resolves the VAP and reports the slot with
   `len == ie_len` (`MATCHES`) - diagnostic/non-gating for the on-air half;
4. `cat $D/cfg80211_status | grep '^app_ie:'` shows
   `param=1 (ap-post) type=4 ... posted=1 refused=0` and `last_rc=0`;
5. page 0x86807FEF, state 1, mask 0; no firmware exception.

## H4 - the on-air check: the laptop scan (decisive for the IE itself)

The AP transmits from the firmware; the class -> frame mapping is the open
question (TABLES T6).  From the **laptop observer** (its Wi-Fi adapter, not
through the router):

    iw dev <laptop-if> scan | grep -i -A2 '00:11:22'
    # or, on air: tcpdump -i <laptop-if> -s 256 -vv \
    #     'wlan type mgt subtype beacon or wlan type mgt subtype proberesp'
    #     and look for the element: dd 0a 00 11 22 33 61 70 70 69 65 21

PASS iff the `dd0a00112233617070696521` element is present in the AP's
**probe response** and/or **beacon** (class 4 first; the vendor appends its
vendor_elements to both buffers).

If the laptop sees the AP but **no 0xDD element**: the host-side message is
accepted (read-back MATCHES) but the firmware does not append class 4 to the
frames this AP sends - record it and do the class sweep in H5 on separate
fresh-boot fires (one class per fire).

## H5 - class sweep (only if H4 saw no element; one class per fire)

Reload with `app_ie_type=` 9, then 0, 1, 2, 3, 5, 6, 7 (one fresh boot each;
class 4 already tested).  The dmesg read-back line prints the slot address for
the chosen class (`slot(class N)=vap+0x...`), so the sweep stays cheap.  PASS
on the first class whose element the laptop sees.  Classes 12/17/18 are path-B
classes and are deliberately **not** supported by this build (the frame would
carry the wrong payload shape - do not sweep them).

## H6 - negative controls (separate fires, one load per boot)

    # app_ie=0 control: no app_ie plan step, no 0x20 frame, the laptop sees no
    # 0xDD element; hostapd still AP-ENABLED (proves the frame is the only change)
    insmod ... app_ie=0 app_ie_type=4 ...
    # app_ie=1 without the ladder pieces (must REFUSE before any frame):
    insmod ... app_ie=1 vap_start_g0=0 ...        # -22, "no frame sent"
    insmod ... app_ie=1 beacon_static=0 ...       # -22
    # bad class (must REFUSE before any frame):
    insmod ... app_ie=1 app_ie_type=20 ...        # -22
    # no vendor element in the hostapd config (empty list):
    #   dmesg: "app_ie=1 but the AP settings carry no vendor IE ... step is skipped"

(Each retry needs a fresh power-cycle: the module cannot be unloaded after
the CPU release.)

## Abort rules (inherit)

page not 0x86807FEF / state != 1 / mask != 0 -> power-cycle, record, no retry
in place; never write CA 0x400392f0; never read the RC misc window; no fire
without bench link PASS + recovery card READY + human present; record the
probe output under `build/linux-kbuild/waves/<phase>/` (gitignored).

## What to record on a PASS

module sha256/size/param count (H0); the H1 knobs readback; the
`collected ... vendor IE` line; the complete plan line **with `app_ie`
between `vap_start_g0` and `band_start`**; the `'app_ie' posted` line
(33 B, g0 id 0x20, ie_len/type/mode/class); the `app_ie post` read-back line;
the sysfs `app_ie:` line; hostapd `AP-ENABLED`; the laptop scan output with
the `dd0a00112233617070696521` element (12-B TLV); page/mask golden; UART capture path.
That set is the app-IE evidence pack - host-side closed iff 1-5 hold; on-air
closed iff H4 sees the element.

## Why one variable

The only change vs the R5/IMAGE-#3 module is the added g0 id 0x20 frame (plus
its fail-closed parameters, the plan step, the read-back and the sysfs line).
The AP-start ladder, the beacon template, the channel config and the data
path are unchanged, so an app-IE verdict is unambiguous.
