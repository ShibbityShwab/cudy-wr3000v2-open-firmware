# REPORT - the H2D g0 id 0x20 app/vendor IE injection: decode, integration, limits

**Lane:** wifi-app-ie, shared task **task-234**, 2026-11-26.  **HOST-ONLY**:
this lane never contacted the router, never wrote firmware, never produced an
image and never flashed.  The vendor .ko / FIRMWARE.bin stay local evidence
(gitignored); the committed outputs are the decode, the clean-room source, the
patch, the tests and the text evidence.  All quoted frames are decoded protocol
bytes as hex text with MACs/pointers absent.

## 1. Why this lane exists

The first-ever stock H2D capture (sibling lane **wifi-stock-h2d-census**,
task-232, commit e977557) decoded the vendor families our clean-room stack never
sends.  The most useful one for AP completeness is **g0 id 0x20
`hmac_config_h2d_send_app_ie` (builder CA 0x600cc)**: the vendor path that
installs the per-VAP IE table the firmware appends to management frames.  Our
stack builds its beacon/AP frames through the host-posted template ladder
(0x0127/0x0124/0x0065/0x0242) and has no way to hand the firmware extra IEs
(vendor_elements/WPS/custom IEs).  This lane decodes that message fully,
integrates it with the cfg80211 AP-start path, and ships the fire recipe.

## 2. The decode (details: TABLES.md)

### 2.1 The wire format [V]

```
 0 1 2 3 | 4 5 | 6 7 | 8 9 | 10 11 | 12..15 | 16 | 17 | 18 19 | 20.. | +3
 group   | total| 0x20| b8b9| 5a5a  | ie_len |type|mode| pad   | ie   |tail
 (b1/b2/b3 = ctx[2]/ctx[1]/ctx[0]; b3 = the VAP/module index)
```

* allocation = `ie_len + 11`; frame total = `ie_len + 23`;
* payload = `u32 ie_len; u8 type; u8 mode; u8 pad[2]; u8 ie[ie_len]` plus the
  **3-byte zero tail** the builder copies past the used caller struct;
* captured stock frame (23 B, ie_len 0, type 4, mode 0, VAP 4) reproduced
  byte-for-byte by our builder (host selftest T1).

### 2.2 The firmware handler [V]

Group-0 receive table entry 27 (`{id 0x20, handler CA 0x85631}`, table CA
0x10CCFC): validates `payload_len <= 616` and `ie_len <= 608`, looks the
VAP up by `hdr[3]`, copies the payload, then:

* mode 0 -> per-VAP class table slot = `vap + 20*type + 0x24c`
  (ptr/len/cap/6-byte field); `ie_len == 0` clears the class, otherwise the IE
  blob is stored (alloc/copy/memset-6/log);
* mode != 0 -> classes 12/17/18 only, payload = 6-byte key + IE (`ie_len-6`);
  decoded but **not shipped** here.

The stock capture's only sample is a **clear of class 4** (`ie_len 0`): the
vendor boot clears a class it does not use.  The class -> frame mapping is
**[U]** (TABLES T6); the callers show classes 3/4/9 used by the association
builders, class 1 by scan, class 7 by WPS/P2P and class 16 mirrored to
`vap+0x38c`.  That is why the class is an explicit parameter, default 4.

### 2.3 The vendor caller chain [V]

Vendor userspace drives it through the Hi5622/WEXT ioctl `nlwifi_set_app_ie`
(softapd).  In the driver:
`hmac_config_set_vendor_ie`/`_wps_ie`/`_wps_p2p_ie` ->
`hmac_config_set_app_ie_to_vap` -> (a) `mac_vap_save_app_ie` (host mirror of
the same table) and (b) `hmac_config_h2d_send_app_ie` (the H2D frame).  The
scan path posts class 1; the host assoc builders append classes 4+9 (response)
and 3+9 (request) via `mac_add_app_ie`.

## 3. The integration

### 3.1 The cfg80211/nl80211 mechanism [V]

6.18 has **no** `NL80211_ATTR_VENDOR_IE`/`SET_VENDOR_IE`.  The AP vendor IEs
arrive in the `start_ap`/`change_beacon` `struct cfg80211_beacon_data`:
`tail` (`NL80211_ATTR_BEACON_TAIL`), `beacon_ies` (`NL80211_ATTR_IE`),
`proberesp_ies` (`NL80211_ATTR_IE_PROBE_RESP`), `assocresp_ies`
(`NL80211_ATTR_IE_ASSOC_RESP`).  hostapd fills them from its
`vendor_elements=` option (beacon tail + probe-response IEs) - so a manual
hostapd conf with `vendor_elements=dd...` is the whole userspace side.
(hostapd's ubus `set_vendor_elements` updates a running AP and needs a
`change_beacon` driver op that this driver does not implement; the fire
therefore uses the start_ap path.)

### 3.2 Our driver [V code, [I] placement]

* new portable module `src/hi5622_app_ie.{h,c}` (also embedded in the patch):
  frame builder, cfg80211 0xDD-IE collector, fail-closed gate, status line;
* `patches/0001-app-ie.patch` on the pinned R5-order base:
  - `hi5622_app_ie.c/.h` compiled into the module (Kbuild);
  - `hi5622_scan.c`: params `app_ie` (0/1, default 0) and `app_ie_type`
    (0..19, default 4); a fail-closed parameter gate **before any frame**;
    collection of the 0xDD IEs from `settings->beacon.{tail,beacon_ies,
    proberesp_ies,assocresp_ies}`; one ladder step between `vap_start_g0`
    (key 0x0065 VAP start) and `band_start` (key 0x0242); a dmesg line and a
    read-only `app_ie:` status line in `cfg80211_status`;
  - `hi5622_scan_logic.{c,h}`: the pure ordering plan gains
    `HI5622_BEACON_STEP_APP_IE` between the 0x0065 and 0x0242 steps (so the
    dmesg plan line names it), the plan function takes the app_ie latch.

Placement rationale [I]: the firmware handler attaches the IE table to the VAP
by `hdr[3]`, so the VAP must exist; the vendor treats it as VAP configuration
(posted before the VAP start); the band start is the last step before the TBTT
and must see the table.  Our step therefore runs after the started VAP and
before the band arm.  Class meaning stays [U] -> parameter.

## 4. Host tests and build

* `tools/run-host-tests.sh`: **55 checks, 0 failures** - includes the
  byte-exact stock-frame reconstruction, header/payload/geometry checks, the
  collector over a mixed SSID+vendor+HT list, the full fail-closed matrix
  (app_ie=2, missing beacon_static/vap_start_g0, class 20, VAP 256, band 2,
  ie_len 609, malformed TLVs, short buffers, unrequested builds) and the
  cfg80211 composition model.
* `tools/build-app-ie.sh`: reproducible host build on the pinned base -
  **PASS**, 0 warnings / 0 errors, 67 parameters (65 base + `app_ie` +
  `app_ie_type`), installed (strip-kmod.sh) **368,348 B** sha256
  `0c8390e8...`, `.text` sha256 `7867d4f0...` (see BUILD-NOTES.md).
* the step also logs a **read-only firmware slot read-back** (`vap + 0x24c +
  20*type` ptr/len/cap) for the fire.

## 5. Limits / what the fire proves

| claim | grade |
| --- | --- |
| the frame format and the firmware acceptance/storage path | [V] |
| the class -> on-air frame mapping | [U] - the fire starts with `app_ie_type=4` (stock-observed) and sweeps if the IE does not appear |
| our round trip (0xDD IE in hostapd config -> frame -> firmware slot) | [I] until the fire's read-back/probe lines and the laptop scan are recorded |
| path B (mode != 0) and the class-16 vendor mirror | decoded [V], intentionally not shipped |

The on-air verdict is the Lead's attended fire (FIRE-RECIPE.md): one load,
`app_ie=1`, a 2.4 GHz HE AP with hostapd `vendor_elements=dd...`, a laptop
scan/probe capture showing the 0xDD IE, and - if the laptop side is
inconclusive - a read-only firmware slot probe.

## 6. Evidence index / reproduction

| evidence | content | regenerate |
| --- | --- | --- |
| `evidence/HOST-TESTS.txt` | 55/55 host checks + the rebuilt stock frame hex | `tools/run-host-tests.sh` |
| `evidence/BUILD.txt` | base/lane pins + artifact shas + param count | `tools/build-app-ie.sh` |
| `evidence/FRAMES.txt` | the captured stock frame, the rebuilt reference, the vendor-IE frame, the handler anchors | quoted from the commands in TABLES.md |
| `TABLES.md` | T1-T9 decode tables | - |
| `patches/0001-app-ie.patch` | the kernel integration | verified: applies to the pinned base and reproduces the built module |
| `/tmp/h2d-stock.log` | the capture itself (local, gitignored) | sibling lane task-232 |
| vendor wifi.ko / FIRMWARE.bin | local evidence only | never committed |
