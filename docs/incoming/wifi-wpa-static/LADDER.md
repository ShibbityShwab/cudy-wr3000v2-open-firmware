# wifi-wpa-static - attended bench ladder (DESIGN ONLY, not fired)

Machine-readable: [ladders/wpa2-ap.json](ladders/wpa2-ap.json).  **This lane
fired nothing**: no device contact happened while producing this document.
The rungs below are for an attended session with the recovery door READY and
the bench link PASS - the project physical rules (AGENTS.md, BENCH-FACTS.md)
override anything written here.

## Why this ladder exists

The M3 blocker recorded by wifi-image-polish was: hostapd AP-ENABLED works but
WPA2 stops at key installation because the driver has no
`.add_key/.set_default_key` ops.  This lane decoded the vendor path and
implemented the three ops behind `wpa_keys=0` (fail-closed).  The ladder
turns the static decode into a hardware fact in two rungs (R1 refusal, R2
install) and keeps the rest honest.

The key point for scheduling: **hostapd installs the WPA2 group key at AP
startup** (`wpa_init_keys()` right after NEW_BEACON), so R2 needs no client
and no RX path - it is the next available hardware gate.  The 4-way
handshake/PTK needs the RX path and is explicitly out of reach (R3).

## Module incantation (recorded before the fire; one load per boot)

    # common
    insmod /lib/modules/6.18.60/hi5622_wifi.ko \
        allow_frames=32 h2d_send_budget=10 hcc_trace=1 \
        wpa_keys=<0|1> wpa_key_module=0

* R1 uses `wpa_keys=0` and must never write a key frame.
* R2 uses `wpa_keys=1`; the 4 AP-start frames + 2 key frames fit the
  32-frame budget (the AP sequence uses 4, hostapd retries are not expected).

## Rungs

| rung | fixture | wpa_keys | what must happen | what must not happen |
| --- | --- | ---: | --- | --- |
| R0 | open AP | 0 | unchanged AP-ENABLED baseline, page golden, mask 0 | any key frame/log |
| R1 | WPA2 | 0 | start_ap 4 frames + AP-ENABLED, then `add_key refused - wpa_keys=0`; hostapd sees -EOPNOTSUPP | any `key_add`/`key_default` announce; page/mask change |
| R2 | WPA2 | 1 | AP-ENABLED, then `key_add` (cmd 0x0078, 96 B, idx=1 pairwise=0 CCMP class=data) and `key_default` (cmd 0x0079, 68 B, idx=1 multi=1); hostapd alive >=120 s; status add_ok=1 def_ok=1 | post refusal, H2D lane close, exception, page/mask change |
| R3 | WPA2 + client | 1 | (blocked today) needs mgmt RX + EAPOL; pairwise key_add would be refused for an unknown STA MAC | pretending the handshake works |
| R4 | WPA2 | 1 | teardown via controlled stop_ap + 60-min soak, no new noise class | key path hot-looping, leaks |

## Expected dmesg lines (R2)

    cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=... tail=...)
    cfg80211: start_ap sequence frame 1/4 'ap_ssid' posted (36 B, ssid_len=..., ...)
    cfg80211: start_ap sequence frame 2/4 'ap_cfg' posted (184 B, ...)
    cfg80211: start_ap sequence posted to provider ... - EXPERIMENTAL
    cfg80211: add_key posted (g0 id1 cmd 0x0078 'key_add', 96 B, module=0 idx=1 pairwise=0 cipher=0x000fac04 class=data key_len=16 seq_len=0) - firmware worker CA 0x0A5825 installs the key; no key material is logged
    cfg80211: set_default_key posted (g0 id1 cmd 0x0079 'key_default', 68 B, module=0 idx=1 uni=0 multi=1) - firmware worker CA 0x0A5C9D selects the default key

The key_add/key_default order is fixed by hostapd (NEW_KEY then SET_KEY); a
`del_key` line is only expected for a known STA teardown.

## PASS criteria (R2)

1. page 0x86807FEF with mask 0 before and after, no exception lines;
2. exactly one 0x0078 and one 0x0079 announce consumed (no refusal line);
3. `cfg80211_status` (sysfs) shows `wpa_keys=1 add_ok=1 add_err=0 def_ok=1
   def_err=0 last_op=set_default_key last_errno=0`;
4. hostapd stays alive >=120 s with no "Failed to set key" after the key
   lines and no "Failed to set beacon parameters" restart;
5. optional (does not gate PASS): firmware key-cache readback at
   `user+0x310/+0x311` (cipher 0x04, default index 1) through the
   bench-proven `module_ctx+0x190` readback recipe.

## Abort policy

* page != golden or mask != 0, H2D lane closed, firmware exception, or any
  bench link loss: **stop, capture, power-cycle** - never `rmmod` after the
  release and never retry the same frame in the same boot;
* no human at the bench / recovery door not READY: no fire;
* record the exact refusal/error line verbatim; a refusal is a valid result.

## Not covered here

* WPA3-SAE (needs the management-frame auth path),
* PMF/BIP management keys (`set_default_mgmt_key` is deliberately not
  registered; the vendor worker wants a cipher byte the vendor builder does
  not fill),
* the 4-way handshake/EAPOL (RX path),
* STA connect (join payload still [U]).
