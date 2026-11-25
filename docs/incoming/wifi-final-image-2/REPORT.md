# REPORT - FINAL IMAGE #2 (wifi-final-image-2, task-159)

**Status: BUILT + VERIFIED host-side (verify 82/82 PASS; selftest 49 passed,
0 failed, 1 skipped).
NOT flashed, NOT booted by this lane.** Kernel slice **UNCHANGED**
(`13e81214...` / slot `1794a531...`); rootfs-only successor of the
wifi-hostapd-sae image B (`8944740e...`). The image carries the **corrected
final association module** `ab9e18a4...` (56 params) as
`kmod-hi5622-wifi`, the **WPA2/WPA3 hostapd fixtures** and the **hi5622
loader/init with autoload=0**.

Headline identities:

    container   7,188,799 B  sha256 0d9429ec4681d43966bd2f23dc3fbe872f232961eaf142c16f565c67be5b5a15
    kernel      3,372,638 B  sha256 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f  (UNCHANGED, slot 1794a531...)
    rootfs UBI  3,801,088 B  sha256 44df3a63cfd9b9811a4ad99d8f49ae917a49c4408719d0df4582ed50785faedb
    module art.   344,372 B  sha256 ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e  (56 params)
    module inst.  325,932 B  sha256 4f1b45c187e9dfc61783a458f6a60056763f1da037bebec239b5b571f4ff4de1
    manifest     83 packages (hostapd-mbedtls SAE, iw, ucode wifi-scripts, regdb, kmods)

---

## 1. What this image is

The bench image line is a ROOTFS-ONLY chain over one hardware-validated kernel
slice:

    ae5e28bf (wifi-image-integration)
      -> 0efcd1f1 (wifi-final-image: all-patch module 5b587949 + hostapd fixtures)
      -> a31bc405 (image A: + SAE hostapd; 8944740e = image B: + wpa3-sae module)
      -> 0d9429ec (THIS image #2: corrected association module ab9e18a4)

Everything except the rootfs is identical across the chain; the kernel slice is
the same `13e81214...` bytes that the ABI image install already wrote, so a
rootfs-only flash (mtd14 / pair b) is sufficient - see
[FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md).

**Delta versus image B (`8944740e`):**

1. `kmod-hi5622-wifi` = the corrected FINAL ASSOCIATION module (the image-B
   `wpa3-sae` module `c7124bb9` is superseded; it is subsumed by this chain).
2. The hi5622 loader/init/uci policy retargeted to the 56-param module; the
   association gates (mgmt_tx/mgmt_rx/sta_ops/eapol_rx/sta_user_del/data_tx)
   added as **fail-closed** uci options.
3. The six WPA2/WPA3 hostapd fixtures are carried (image B carried the SAE
   three; this image carries those **plus** the proven open and WPA2 fixtures,
   all byte-identical to committed payload text).
4. `autoload=0` remains the shipped policy: nothing loads at boot.

## 2. The corrected final association module (the point of image #2)

| property | value |
| --- | --- |
| artifact | `/tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko` - 344,372 B sha256 `ab9e18a4...` |
| params | **56** (47 all-patch + 9 association) |
| vermagic / deps | `6.18.60 SMP mod_unload ARMv7 p2v8` / `cfg80211` |
| installed | 325,932 B sha256 `4f1b45c1...` (.text 144,572 B byte-identical to the artifact) |
| source chain | final all-patch tree (5b587949) + **wpa3-sae** + **ap-mgmt** + **sta-ops** + **data-tx** + the sibling-gpl **corrected mgmt-TX carrier** patch |
| not-yet-fired | this exact combined module; its parts were built/host-tested, the AP/WPA2-GTK/TX/RX and WPA3-on-air milestones were fired on earlier modules of the same chain |

**The corrected mgmt-TX carrier.** The mgmt-TX path in image B's module was the
host-internal WAL cfg-event `0x01F2` descriptor; the sibling-GPL/public-research
wave proved that is the wrong message. The corrected device-facing carrier is a
**raw HCC group 0 / id 6 control message: 12-B HCC header + 48-B device TX
control block + the 802.11 frame inline**; completion is DMAC `0x01F3` ->
host `g2 id13`. The module carries the corrected builder
(`himg_build_hcc_mgmt_tx`) and the verifier asserts the superseded WAL carrier
log string is **absent**. Bench evidence from the carrier wave: the corrected
carrier was **hardware-accepted - posted 86 B, consumed 0 polls** (one mgmt
frame per load).

**What else the module carries (all behind fail-closed params):**
`mgmt_rx` (D2H CRX event `g1 id13` -> `cfg80211_rx_mgmt_ext`), `sta_ops`
(`.add/change/del/get/dump_station` + per-STA firmware context `g0 id7
user_add` + `g0 id1 0x07d2` asoc-state), `eapol_rx` (802.11 -> 802.3 EAPOL
delivery in the rx drain), `data_tx` (`ndo_start_xmit` -> the proven dp TX
ring/TXQ path, EAPOL msg1/msg3), plus the WPA3 PMF/IGTK path
(`.set_default_mgmt_key`, AP `mgmt_stypes=0x3c2a`) proven on air by the
wpa3-sae wave.

Verifier evidence for the installed module: 56 params; symbol
`himg_build_hcc_mgmt_tx`; WAL `0x01F2` carrier string absent; descriptor
carrier (`mgmt_tx: DMA frame buffer`) absent; imports
`cfg80211_mgmt_tx_status_ext`, `cfg80211_rx_mgmt_ext`,
`cfg80211_bss_iter`; depends exactly `cfg80211`; feature strings
`sta_ops: STA %pM added`, `data-tx tx: posts=`,
`cfg80211: set_default_mgmt_key posted`, `DP rx_arm: live bank CA`.

## 3. WPA2/WPA3 hostapd configs carried in the image

All are byte-identical between this lane's committed `payload/` and the built
rootfs (`/etc/hi5622/`):

| fixture | profile | sha256 |
| --- | --- | --- |
| hostapd-open.proven.conf | open, hardware-proven AP-ENABLED baseline | `bfc7faaf...` |
| hostapd-open.conf | open, bench default (`hi5622.ap.config`) | `f776437e...` |
| hostapd-wpa2.conf | WPA2-PSK, CCMP, HT20 - the 4-way association-fire target | `d1f5f67c...` |
| hostapd-wpa2-hardened.conf | WPA2-PSK + **PMF required** (`ieee80211w=2`), BIP-CMAC-256 | `be379728...` |
| hostapd-wpa2-wpa3.conf | WPA2/WPA3 **transition** (WPA-PSK + SAE), PMF optional w/ `sae_require_mfp=1`, BIP-CMAC-256 | `d824fd13...` |
| hostapd-wpa3-sae.conf | **WPA3-SAE only**, MFP required, BIP-CMAC-256 | `6ba5d4f1...` |

Functional state is documented per fixture: WPA3-SAE reached **AP-ON-AIR**
(RSN IE SAE + MFP-required + BIP-CMAC-256 observed externally) with GTK+IGTK
install on the wpa3-sae build; the WPA2 4-way is the target of the first
combined association fire once a client associates. None of these fixtures is a
claim that a secured client joined.

## 4. Loader / init policy (`autoload=0`, association gates fail-closed)

`/usr/sbin/hi5622-load` (called by `/etc/init.d/hi5622-wifi`, rc.d `S96`) is
retargeted to the 56-param module: it spells out **38 explicit insmod args**
(including the 9 new association params), loads `cfg80211` first, verifies the
parameters took effect, runs the C2 ladder, and optionally starts hostapd.

* Shipped policy: `hi5622.main.autoload='0'` - nothing loads at boot
  (`/etc/config/hi5622`; modprobe.d mirrors the same defaults).
* Association gates default: `mgmt_tx=0 mgmt_rx=0 sta_ops=0 eapol_rx=0
  sta_user_del=0 data_tx=0 data_tx_peer=1 data_tx_protect=0
  data_tx_timeout_ms=200` (fail-closed; the image boots exactly as safe as the
  proven baseline).
* Validated feature gates stay on as in the proven recipe:
  `rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1`, budgets
  `allow_frames=32 h2d_send_budget=10`.

The lane selftest runs the loader dry-run and asserts the full argument set,
no `rmmod`, the C2 ladder, uci<->loader<->modprobe consistency and the
fail-closed gate defaults.

## 5. Verification (from the built bytes, 82/82 PASS)

`tools/verify-final-image-2.sh` checks, from the container bytes:

* **kernel slice UNCHANGED** `13e81214...` / 3,372,638 B; rootfs UBI `UBI#`;
  UBI volume == build `root.squashfs` prefix + zero padding; built kernel
  config forces `ubi.mtd=rootfsb` (`CONFIG_CMDLINE_FORCE=y`).
* **userspace carry**: SAE hostapd `451dd49f...` (ARM, mbedtls, SAE strings,
  not the old 504,333 B hostapd-mini); `/usr/sbin/iw` (ARM); `iwinfo` +
  `wifi/hostapd` ucode modules; `regulatory.db`; `cfg80211.ko` +
  `hi5622_wifi.ko`.
* **module identity**: pinned artifact sha/size/params, installed sha recorded,
  56 params, corrected carrier symbol present + superseded carrier strings
  absent, association cfg80211 imports, `.text` byte-identical to the pinned
  artifact.
* **payload**: all 6 hostapd fixtures + `/etc/config/hi5622`,
  `/etc/init.d/hi5622-wifi`, `/etc/modprobe.d/hi5622-wifi.conf`,
  `FIRMWARE-POLICY.txt`, `/usr/sbin/hi5622-load` byte-identical to committed
  payload; rc.d link present; `autoload=0`; `mgmt_tx=0` fail-closed; uci
  names the corrected module sha.
* **manifest** (83 packages): hostapd-mbedtls, hostapd-common, libmbedtls21,
  iw, wireless-regdb, wifi-scripts, kmod-hi5622-wifi, kmod-hi5622-cfg80211;
  **no hostapd-mini**.
* **policy**: only `regulatory.db` under `/lib/firmware`; no
  `FIRMWARE*.bin`; no large blobs under `/lib/modules`.

Full output: [VERIFY.txt](VERIFY.txt).

## 6. Reproducibility

Three recorded bakes (and two earlier runs during verifier development) all
produce the **byte-identical container** `0d9429ec...`, rootfs UBI
`44df3a63...`, squashfs `f0f970e2...`, installed module `4f1b45c1...`
(`build-evidence/build-runs.txt`; 8-10 s rootfs-only builds, kernel not
rebuilt).

## 7. Rootfs-only flash feasibility

Re-checked: **YES**. Kernel member `13e81214...` unchanged -> kernelb/mtd12
slot `1794a531...` already contains those bytes and needs **no write**; the
only changed member is rootfs UBI 3,801,088 B (fits mtd14 0x1740000 =
24,379,392 B with 20,578,304 B free); same container size as image B; forced
cmdline still roots from `rootfsb`. Manual strict sequence and preconditions:
[FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md).

## 8. Association fire recipe for THIS image (next bench step)

One module load per boot, recovery armed, PD4 firmware + local `wifi_block.bin`
pushed by hand, antennas attached (RF was the blocker: ~44 dB loss without
them). Enable the association chain via the image's own uci policy, then use the
normal loader:

    uci set hi5622.main.mgmt_tx=1        # corrected HCC g0 id6 carrier
    uci set hi5622.main.mgmt_rx=1        # D2H CRX g1 id13
    uci set hi5622.main.sta_ops=1        # add/change/del_station + per-STA ctx
    uci set hi5622.main.eapol_rx=1       # 802.11->802.3 EAPOL into rx drain
    uci set hi5622.main.data_tx=1        # ndo_start_xmit -> dp TX (EAPOL msg1/3)
    uci commit hi5622 && /etc/init.d/hi5622-wifi start
    # (equivalent explicit insmod: the ASSOCIATION-INTEGRATION recipe + the new
    #  gates; hcc_trace=1 is recommended for the first association run)

Then start the AP with a WPA2 fixture
(`uci set hi5622.ap.enabled=1; uci set hi5622.ap.config=/etc/hi5622/hostapd-wpa2.conf`
before the load), and have the external client associate.

Expected evidence per rung: page `0x86807FEF/7/2/0xABCD` + mask 0; AP-ENABLED
with the 5-message vendor order; `frame post 'mgmt_tx': g0 id6 total 86 B ...
consumed after 0 polls` (auth/assoc response); `cfg80211: mgmt RX delivered
(subtype ...)`; sta_ops add; `data_tx_status` posts/completed advancing;
hostapd 4-way (WPA2) or SAE (WPA3). Controls: with the gates at 0 the same load
must keep `.mgmt_tx` fail-closed (`-EOPNOTSUPP`) - no post.

**Stop rules unchanged:** anything unexpected on the page/mask/exception path ->
power-cycle, no retry in the same boot; the module boot ladder and H2D lane are
one-shot.

## 9. Limits / honest notes

* **Nothing was fired by this lane**; the image is a host-built, host-verified
  instrument. Association/4-way is still a bench result to capture - blocked on
  RF (antennas being attached) and the flaky client MT7922.
* The corrected carrier was hardware-accepted (86 B posted, consumed 0 polls)
  in the carrier wave; this **combined** module has not been re-fired.
* WPA3-SAE on-air (SAE AKM, MFP-required, BIP-CMAC-256) is proven on the
  earlier wpa3-sae module; this module includes that code path and the corrected
  mgmt TX, but the combination is a pending bench step.
* The **rx-watermark lane (task-156)** is rebuilding the module as assoc +
  corrected carrier + **RX refill watermark** (new `rx_refill_wm`, default 75,
  inert unless the operator enables/runs the RX drain). If that build lands, it
  supersedes this module for the image; `tools/bake-final-image-2.sh` accepts
  `DRIVER_KO/DRIVER_SHA/DRIVER_SIZE/DRIVER_PARAMS` pins and re-bakes from the
  same tree in ~10 s. **This image is the current baked baseline (`ab9e18a4`).**
* Vendor-derived PD4 firmware and `wifi_block.bin` are never packaged (manual
  push after flash, overlay wipe removes them - by policy).
* Bench-only image; not an upstream artifact.

## 10. Evidence / reproducibility index

| file | content |
| --- | --- |
| [HASHES.txt](HASHES.txt) | authoritative container/rootfs/kernel/module/payload hashes |
| [VERIFY.txt](VERIFY.txt) | verifier output over the built bytes (82/82) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact inputs, command sequence, reproducibility, tree state, module rebuild recipe |
| [FLASH-FEASIBILITY.md](FLASH-FEASIBILITY.md) | rootfs-only install re-check + manual sequence |
| `build-evidence/` | build log, run history, driver identity, payload manifest, kernel identity, applied config delta |
| `payload/` | the text rootfs overlay (loader/init/policy + WPA2/WPA3 fixtures) |
| `tools/bake-final-image-2.sh` | serialized rootfs-only bake + verify + evidence (re-runnable, module-pin override) |
| `tools/verify-final-image-2.sh` | verifier over the built bytes (also re-runnable standalone) |
| `tools/selftest.sh` | host-only payload/policy/loader/module checks |

Local (not committed) artifacts: `/home/shibbityshwab/wifi-final-image-2-artifacts/`.

## 11. Policy

Text only in git; no vendor binary/firmware/calibration byte, no secret. The
fixtures contain only the project's throw-away test passphrases; the driver
module, image, UBI and squashfs are local artifacts and never committed.
