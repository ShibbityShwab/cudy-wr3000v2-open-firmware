# wifi-wpa3-sae - WPA3-SAE / WPA2+WPA3 / WPA2-hardened hostapd profiles + SAE host-side bits

**Lane:** wifi-wpa3-sae (shared task task-144), **host-only**, 2026-11-28.
**Scope:** build the three security-profile hostapd fixtures, implement the
host-side bits WPA3/PMF needs from the clean-room driver (AKM/cipher
advertisement, correct AP management TX bitmap, the PMF default-management-key
path), static-RE what the firmware/host must do for SAE/PMF beyond the proven
GTK, state the exact functional matrix, design (not fire) the bench ladder.

**No device was touched in this lane. No vendor binary/firmware/calibration
byte is committed. No credential is committed (fixtures carry a lab
placeholder only).** Derived addresses/offsets/masks are in
[TABLES.md](TABLES.md); local vendor artifacts and the built .ko stay
local/gitignored as project policy requires.

Base: the pinned FINAL all-patch module (opensource `008aca5`, stripped
308,080 B sha256 `5b587949...`, 47 params).  This lane's patch applies on top
of that chain; the artifact it produces is local evidence and **has not been
fired**.

---

## 0. TL;DR (ten answers)

1. **Three fixtures built and host-validated**: WPA3-SAE only
   (`ieee80211w=2`, SAE AKM only, BIP-CMAC-256), WPA2/WPA3 transition
   (`WPA-PSK SAE`, `ieee80211w=1`, `sae_require_mfp=1`), WPA2-hardened
   (`WPA-PSK`, `ieee80211w=2`).  All parse and pass hostapd's
   `hostapd_config_check()` under a host build of the tree-source hostapd
   v2.12 with CONFIG_SAE+CONFIG_IEEE80211W; two negative controls are refused
   (evidence/config-gate.txt, 31 checks).
2. **The exact reason a PMF/WPA3 profile cannot reach AP-ENABLED today is
   found and closed host-side**: hostapd's `wpa_group_config_group_keys()`
   installs the GTK *and then the IGTK* whenever MFP is enabled, and the IGTK
   default selection goes out as `NL80211_CMD_SET_KEY +
   NL80211_KEY_DEFAULT_MGMT`, which cfg80211 refuses with -EOPNOTSUPP unless
   the driver registers `.set_default_mgmt_key`.  The GTK half was already
   proven; the missing op made `wpa_init_keys()` fail and tore the interface
   down.  This lane registers the op behind the existing `wpa_keys=1` gate.
3. **New firmware decode (derived facts only)**: the default-key worker
   (CA 0x0A5C9D) mgmt branch needs a management cipher byte at body+0x04 -
   bound <=0x0d and in the mgmt class mask 0x3840, with cipher byte 0
   **rejected** (worker error 0x520b).  The vendor/host order gives the host
   the cipher only from the preceding IGTK add_key, so the driver latches it
   and refuses the default-mgmt frame when unknown (never posts a guessed
   byte).  See evidence/fw-default-mgmt-decode.txt.
4. **The PMF IGTK install itself needs no new firmware message**: the existing
   add-key builder already accepts mgmt-class ciphers (AES-CMAC 6,
   BIP-GMAC-128 11, BIP-GMAC-256 12, BIP-CMAC-256 13) at key indexes 4..5;
   the wire is the bench-proven g0 id1 family (cmd 0x0078, 0x44 body).
5. **wiphy AKM/cipher advertisement implemented** (not a hostapd AP gate,
   proven from the hostapd source): the wiphy advertises PSK, with an AP
   iftype override adding SAE; the cipher list gains the two BIP-GMAC suites
   the firmware mgmt class supports (BIP-CMAC-256 was already advertised).
   hostapd's AP path never reads these, so the AP-setup gate never caught the
   missing advertisement - but wpa_supplicant's STA path does, and its
   hardcoded fallback excludes SAE.  This is the correct capability contract
   for the STA side and for userspace tooling (`iw phy`).
6. **AP management TX bitmap fixed**: the AP `mgmt_stypes.tx` was a copy of
   the rx mask (assoc/probe request bits), but cfg80211 checks the TX bitmap
   for the subtypes hostapd sends.  The response subtypes ((re)assoc resp,
   probe resp, disassoc, auth, deauth, action -> `0x3c2a`) are now set; auth
   (SAE commit/confirm) is subtype 11 and was already inside the old mask, the
   rest were not.
7. **SAE's host-side needs beyond the keys are now pinned down**: AP SAE is
   host software (no firmware crypto); it needs AP management TX
   (`wal_cfg80211_mgmt_tx` family, being decoded by the wifi-ap-mgmt lane)
   and management RX, plus association.  Association also needs cfg80211
   `.add_station`/`.del_station` plus the firmware per-STA user context:
   without the op hostapd logs "Could not add STA to kernel driver" and sends
   no assoc response.  This lane does not fake any of that.
8. **Exact functional state (TABLES T8)**: config-valid + key-setup-complete
   is the new boundary this patch reaches (GTK + IGTK + default mgmt key at
   AP startup, fireable without a client); a *usable* WPA3 AP (SAE
   authentication, association, 4-way PTK, data) remains blocked on the
   mgmt-TX/RX + add_station + RX/EAPOL lanes.  Beacons: the RSN IE is carried
   to the firmware by the proven `ap_cfg` message, but on-air observation is
   a separate running lane.
9. **Host verification**: portable key-frame selftest 44/44 (cipher classes,
   the new mgmt body at +0/+2/+4, g0 id1 envelope, refusals); lane
   run-host-check 23/23 (selftest + patched-source assertions + config gate);
   config gate 31/31; patch applies cleanly to the pinned chain and the module
   builds warning-free through the project toolchain (stripped 311,108 B
   sha256 `08832e75...`, identical on two runs).
10. **Honest gaps**: the patch is host-built only; the IGTK/default-mgmt wire
   path is static-decoded, not fired; `sae_pwe=1` strict H2E needs RSNXE
   transport that the AP-config message does not have today (fixtures use
   `sae_pwe=2`); and association/SAE remains a multi-lane blocker.  See
   TABLES T9.

---

## 1. Method and local artifacts

Host-only static analysis plus a host-side hostapd build:

* firmware key workers disassembled locally from the pinned firmware image
  (CPU = file offset + 0x40000, Thumb; only derived offsets/masks/error codes
  are committed);
* hostapd v2.12 source read from the same tarball the bench image tree
  prefetches (`dl/hostapd-2026.08.07~831364bf.tar.zst`, sha256 `d1b9ca0e...`)
  for the AP key-install and capability call chains;
* Linux 6.18.60 cfg80211 sources (the project compile target) for the
  contracts in TABLES T1;
* the previously committed lanes: wifi-wpa-static (key wire layouts, GTK
  proof), wifi-beacon-static (AP-start message family + RSN IE slot +
  vendor-host mgmt path), wifi-image-polish (hostapd fixtures/validator
  precedent, PMF-M3 blocker), wifi-re-report v5 (current project truth).

Cross-checks used: the vendor hmac path emits the same 0x0078/0x0079 commands
(hmac_user_add_key -> hdpp_config_send_event; hmac_enc_11i_default_key_msg ->
hmac_config_send_event) [V, wifi-wpa-static T2], and hostapd's order is fixed
by the state machine (NEW_KEY then SET_KEY) [V, T2].

## 2. What this lane delivered

| file | role |
| --- | --- |
| `configs/hostapd-wpa3-sae.conf` | WPA3-Personal (SAE only), PMF required, CCMP, BIP-CMAC-256 |
| `configs/hostapd-wpa2-wpa3.conf` | WPA2/WPA3 transition (PSK+SAE), PMF optional with SAE-MFP requirement |
| `configs/hostapd-wpa2-hardened.conf` | WPA2-PSK/CCMP with PMF required (hardest WPA2 fallback profile) |
| `patches/0001-wpa3-sae.patch` | 9 files on the pinned final all-patch chain: AKM/cipher advertisement, AP mgmt TX bitmap, `.set_default_mgmt_key`, mgmt default-key body variant, management-cipher latch, op-map row |
| `src/` | the patched kernel sources for review (mirror of the patch result) |
| `tools/build-hostapd-validator.sh` | host x86_64 hostapd v2.12 from the OpenWrt dl/ tarball, CONFIG_SAE+CONFIG_IEEE80211W |
| `tools/validate-configs.sh` | parse + hostapd_config_check gate for the three fixtures, two negative controls, profile assertions |
| `tools/wpa3_selftest.c` | portable selftest of the new mgmt default-key builders + unchanged WPA2 frames (44 checks) |
| `tools/run-host-check.sh` | selftest + patched-source static assertions + config gate (23 checks) |
| `tools/build-wpa3-sae-module.sh` | chain-identity check, patch application, 6.18.60 ARM build, identity/symbol/string gates |
| `TABLES.md` | derived constants/contracts/decode/state tables |
| `LADDER.md`, `ladders/wpa3-ap.json` | attended bench ladder design (not fired) |
| `evidence/` | config gate, module identity, firmware decode summary, selftest |

The patch is purely additive on the pinned chain and leaves every existing
default fail-closed (`wpa_keys=0` is unchanged; the new op is behind the same
gate; the latch starts empty; no wire frame is built unless a bench session
enables the key path).

## 3. Hostapd fixtures and what the host gate proves

The three fixtures share the project's bench shape: `driver=nl80211`,
`interface=wlan0`, 2.4 GHz channel 6, HT20 + short-GI-20 (the wiphy's shipped
HT skeleton), placeholder passphrase `CHANGEME-lab-only`.  Differences are the
security profile only (TABLES T7).

The gate runs the tree-source hostapd v2.12 against each file with a dummy
non-existent interface: config parsing and `hostapd_config_check()` run
before any driver call, so reaching the pre-driver stop is the PASS boundary;
a deliberately broken option and a semantically invalid WPA-PSK-without-PSK
config are both refused (negative controls).  This proves config validity and
hostapd's own WPA/PMF combinations, nothing about the driver.

## 4. The WPA3/PMF startup gate this patch closes

Detailed in TABLES T1/T2/T4.  In short:

```
hostapd_setup_interface()
  hostapd_start_beacon()          -> NL80211_CMD_NEW_BEACON -> driver start_ap
                                     (bench-proven 5-message plan, AP-ENABLED gate)
  wpa_init_keys()
    wpa_group_config_group_keys()
      GTK   add_key idx 1 CCMP    -> wire 0x0078  [bench-proven]
      GTK   set default multicast -> wire 0x0079  [bench-proven]
      IGTK  add_key idx 4 BIP     -> wire 0x0078  [static-decoded here]
      IGTK  set default MGMT      -> wire 0x0079  [needs .set_default_mgmt_key]
```

Before this lane the last step returned -EOPNOTSUPP at cfg80211 and
`wpa_init_keys()` failed, so **any** PMF profile (WPA3-SAE, transition with
ieee80211w=1, WPA2 hardened with ieee80211w=2) tore itself down after the GTK
was already installed.  The patch registers the op, builds the
management-class body (T5/T6) and posts it through the existing fail-closed
frame gate.  The next fire (ladder R2) turns the last three lines from static
decode into hardware fact without needing any client.

## 5. Static RE: what SAE/PMF needs beyond the GTK

* **SAE authentication is host software**: commit/confirm are auth frames
  carried by the AP management TX path and the AP management RX path.  The
  firmware does not do SAE; there is no SAE firmware message.  The vendor's
  AP-side auth/assoc is host software too (`hmac_ap_up_rx_mgmt` ->
  `hmac_mgmt_encap_auth_rsp`/`..._asoc_rsp_ap` -> `wal_cfg80211_mgmt_tx`,
  wifi-beacon-static T10), so the WAL mgmt-tx family being decoded by the
  wifi-ap-mgmt lane is the direct SAE enabler.
* **PMF management protection**: the AP generates the IGTK and installs it
  locally through add_key (this patch's path); the firmware mgmt-class key
  cache (`user+0x312/0x313`) is where it lands.  Unicast management-frame
  protection itself needs the mgmt TX path (and RX) to carry protected
  frames; that is downstream of the same mgmt family.
* **Association** needs more than mgmt TX/RX: hostapd adds each associated
  station through NL80211_CMD_NEW_STATION, which requires the cfg80211
  `.add_station` op and a firmware per-STA user context; without it hostapd
  refuses the association (no assoc response).  The clean-room driver does not
  implement station ops today - recorded in TABLES T9 as an explicit gap so
  the next lane does not assume "mgmt TX = association".
* **Per-STA PTK/IGTK** remain M3/M4 work: the wire builders exist
  (wifi-wpa-static) but the firmware user must exist first (add_station) and
  the 4-way needs EAPOL over the RX data path.
* **Strict H2E**: hostapd advertises SAE-H2E through RSNXE in the beacon tail;
  the bench-proven AP-config body only carries one RSN IE slot plus the WMM
  vendor slot, so RSNXE is dropped today.  The SAE fixtures therefore use
  `sae_pwe=2` (both), and strict `sae_pwe=1` is recorded as gap G-RSNXE.

## 6. Limits and evidence markers

* The firmware decode is static ([V] from the pinned local image); the IGTK /
  default-mgmt wire path is **not** hardware-proven.  Ladder R2/R3/R4 are the
  fires that would make it [V*].
* The hostapd call-chain facts are read from the v2.12 source tarball the
  project image tree uses ([V], function-level), not from the vendor 2.5.24
  userspace (the vendor's own WPA3 capability is not claimed).
* AKM/cipher advertisement is a capability-contract fix with no AP-side gate
  on the hostapd path; the STA-side gate is real but the STA connect path is
  still fail-closed in this driver, so the advertisement is a correctness
  prerequisite, not a new feature.
* The module artifact is local evidence only, not committed and not fired.

## 7. Reproduce

    cd opensource/docs/incoming/wifi-wpa3-sae
    ./tools/run-host-check.sh --evidence evidence/host-check.txt   # fast, host-only
    ./tools/build-wpa3-sae-module.sh                               # chain check + module build
    ./tools/validate-configs.sh --evidence evidence/config-gate.txt

The build script discovers the project 6.18.60 kernel tree and toolchain, or
accepts `KDIR_OVERRIDE`/`STAGING_OVERRIDE`; `CHAIN_TREE` can point at an
already-staged final all-patch tree.  Nothing is committed from `/tmp`.

## 8. Coordination

* **wifi-ap-mgmt (task-143)** owns the `.mgmt_tx`/`.mgmt_rx` implementation and
  the WAL management-frame decode; this lane took the complementary,
  non-overlapping ground: wiphy AKM/cipher advertisement, AP mgmt TX bitmap,
  `.set_default_mgmt_key` + management-class key body.  The two patches are
  additive and can be applied in either order on the pinned chain; this lane
  does not touch `.mgmt_tx`, `.mgmt_rx`, the D2H hooks or station ops.
* The `add_station`/association gap (TABLES T9) is reported here because it
  is the next blocker after mgmt TX/RX and should be planned explicitly.
