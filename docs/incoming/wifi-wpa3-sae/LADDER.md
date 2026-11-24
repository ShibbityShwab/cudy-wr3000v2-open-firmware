# wifi-wpa3-sae - attended bench ladder (DESIGN ONLY, not fired)

Machine-readable: [ladders/wpa3-ap.json](ladders/wpa3-ap.json).  **This lane
fired nothing**: no device contact happened while producing this document.
The rungs are for an attended session with the recovery door READY and the
bench link PASS; the project physical rules (AGENTS.md, BENCH-FACTS.md)
override anything written here.

## Why this ladder exists

The F16 milestone proved the WPA2 GTK path at AP startup.  The static work in
this lane found why **every PMF/WPA3 profile** could not have reached the same
state: hostapd also installs the IGTK at AP startup, and the IGTK's default
selection needs the driver `.set_default_mgmt_key` op that did not exist.
The patch adds it and the management-class default-key frame
(TABLES T4/T5/T6).  The ladder turns that static decode into a hardware fact
with **no client**, then documents the client/SAE gate honestly.

The key scheduling point: WPA3-SAE authentication still needs the AP
management TX/RX path (wifi-ap-mgmt lane) plus association and
`.add_station`; none of the rungs below fake that.

## Fire recipe (same as the final all-patch validation, security profile swapped)

Fresh golden boot, recovery armed, PD4 firmware in place by hand
(`/lib/firmware/hi_wifi/FIRMWARE.PD4.bin`, never packaged).  One module load
per boot; use the Lead's fired baseline parameters verbatim and change only
the security-profile knobs:

    insmod /lib/modules/$(uname -r)/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=<0|1> wpa_key_module=0 \
        sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0

Then the proven boot ladder (prepare/upload/verify/seed/release), C2
(`h2d_ctx_create_t1` + `h2d_txq_sync72`), iface `__ap`, and hostapd with the
lane fixture (copy it to the box with `tools/rscp.sh` during the attended
session; never commit a real passphrase - edit the placeholder locally).

Budget: the 5-message AP start + up to 4 key frames fits
`allow_frames=32 h2d_send_budget=10`.

## Rungs

| rung | fixture | wpa_keys | what must happen | what must not happen |
| --- | --- | ---: | --- | --- |
| R0 | open AP (control) | 1 | unchanged AP-ENABLED baseline on the patched module, page golden, mask 0 | any key frame or cfg80211 key log |
| R1 | WPA3-SAE | 0 | AP-start 5-message plan + NEW_BEACON, then `add_key refused - wpa_keys=0`; hostapd reports the key failure; no key frame | any `key_add`/`key_default`/`key_default_mgmt` announce; page/mask change |
| R2 | **WPA3-SAE** | 1 | **PRIMARY**: AP-ENABLED, GTK pair consumed, IGTK pair (`key_add` idx 4 class=mgmt + `key_default_mgmt` idx 4) consumed; hostapd alive >=120 s; status defmgmt_ok=1 | post refusal, exception, page/mask change, hostapd "Failed to set key" after the default-mgmt line |
| R3 | WPA2/WPA3 transition | 1 | same startup key sequence as R2; hostapd alive >=120 s | same |
| R4 | WPA2-hardened | 1 | same startup key sequence as R2; hostapd alive >=120 s | same |
| R5 | any + client | 1 | (BLOCKED today) SAE/4-way association needs AP mgmt TX/RX + `.add_station` + RX/EAPOL | pretending an association happened |
| R6 | R2 profile | 1 | teardown via controlled stop_ap + 60-min soak, no new noise class | key path hot-looping / leaks |

## Expected dmesg (R2, WPA3-SAE profile)

AP start (unchanged, final all-patch order):

    cfg80211: start_ap beacon_static=1 plan (5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start
    cfg80211: start_ap sequence frame 1/5 'vap_add' posted ...
    cfg80211: start_ap sequence frame 2/5 'ap_chan' posted (32 B, band=0 ch=6 ...)
    cfg80211: start_ap sequence frame 3/5 'ap_ssid' posted ...
    cfg80211: start_ap sequence frame 4/5 'ap_cfg' posted ...
    cfg80211: start_ap sequence frame 5/5 'ap_vap_start' posted (after ap_cfg)
    wlan0: AP-ENABLED

Key path (new lines from this patch, order fixed by hostapd):

    cfg80211: add_key posted (g0 id1 cmd 0x0078 'key_add', 96 B, module=0 idx=1 pairwise=0 cipher=0x000fac04 class=data key_len=16 seq_len=0) - firmware worker CA 0x0A5825 installs the key; no key material is logged
    cfg80211: set_default_key posted (g0 id1 cmd 0x0079 'key_default', 68 B, module=0 idx=1 uni=0 multi=1) - firmware worker CA 0xA5C9D selects the default key
    cfg80211: add_key posted (g0 id1 cmd 0x0078 'key_add', 96 B, module=0 idx=4 pairwise=0 cipher=0x000fac0d class=mgmt key_len=16 seq_len=0) - firmware worker CA 0x0A5825 installs the key; no key material is logged
    cfg80211: set_default_mgmt_key posted (g0 id1 cmd 0x0079 'key_default_mgmt', 68 B, module=0 idx=4 cipher=0xd mgmt-class) - firmware worker CA 0xA5C9D selects the PMF default management key

If the IGTK add_key fails on hardware, the exact refusal line is the result
(record it verbatim) - it would mean the firmware mgmt-class branch needs a
different body field, not that the WPA3 config is invalid.

## PASS criteria (R2)

1. AP start 5-message order posted and `wlan0: AP-ENABLED`, ssid
   `cudy-wr3000v2-wpa3` (my lane fixture) or the local edited ssid;
2. exactly one data-class `key_add` idx 1 and one `key_default` idx 1, then
   exactly one mgmt-class `key_add` idx 4 and one `key_default_mgmt` idx 4,
   all consumed (no refusal / NOT posted line);
3. `cfg80211_status` (sysfs) shows
   `wpa_keys=1 ... add_ok=2 add_err=0 del_ok=0 def_ok=1 def_err=0
   defmgmt_ok=1 defmgmt_err=0 ... last_op=set_default_mgmt_key last_idx=4
   last_errno=0`;
4. hostapd stays alive >=120 s with no "Failed to set key" after the
   default-mgmt line and no "Failed to set beacon parameters" restart;
5. clean state before and after: page `0x86807FEF`/golden words, state 1,
   mask 0, staging 0, no exception lines, TX/rx_arm readbacks unchanged.

Optional (does not gate PASS): firmware key-cache readback through the
bench-proven `module_ctx+0x190` recipe - the VAP user's mgmt slot should show
`user+0x312 = 4` (key index) and `user+0x313 = 0x0d` (BIP-CMAC-256), and the
data slot `user+0x310 = 0x04`, `user+0x311 = 1` (this is what the worker
stores, TABLES T5).

## Abort policy

* page != golden or mask != 0, H2D lane closed, firmware exception, or bench
  link loss: **stop, capture, power-cycle** - never `rmmod` after the release
  and never retry the same frame in the same boot;
* no human at the bench / recovery door not READY: no fire;
* a refusal line is a valid result; record it verbatim;
* do not attempt R5 (client association) until the ap-mgmt management
  TX/RX lane and the station-op gap are closed.

## Not covered here

* SAE client association/authentication and the 4-way PTK (mgmt TX/RX +
  `.add_station` + RX/EAPOL paths);
* on-air beacon proof (RSN IE placement), which is the separate on-air
  observer lane's job;
* strict SAE H2E (`sae_pwe=1`): needs RSNXE transport that the AP-config
  message does not carry today (TABLES T7, gap G-RSNXE).
