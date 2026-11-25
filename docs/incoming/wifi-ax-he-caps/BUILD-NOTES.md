# BUILD-NOTES - how the HE-capability module is produced and what is pinned

**HOST-ONLY.**  Everything below runs on the workstation; nothing contacts
the router.  The `.ko` is local evidence and is **never committed**.

## Chain

    ../wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh
      -> /tmp/wifi-vapmsg5g-build          (58-param vapor order tree)
      -> /tmp/wifi-vapmsg5g-artifact/hi5622_wifi.ko
         353,652 B stripped, sha256 13e7bf4c...2cc1abf   <- pinned base
      + this lane's src/hi5622_he_caps.{c,h}   (portable contract helper)
      + this lane's patches/0001-he-caps.patch (he_caps param, default 0)
      = 59-param HE-capability module

`tools/build-he-caps-module.sh` performs the whole chain.  It refuses when
the pinned base artifact drifted (sha256 mismatch) or when the base is not
the 58-param module, so the patch never silently applies to another tree.

## Artifact (host-built; local evidence only)

    stripped   356,564 B  sha256 475975d32c9e56e2de1974d28ea983f1e0e7e36c4a0465ae4eeb7ea19755cdea
    unstripped 1,365,916 B
    vermagic   6.18.60 SMP mod_unload ARMv7 p2v8 ; depends=cfg80211
    params     59 (the 58 vap5g params + he_caps, default 0)
    0 compiler/modpost warnings; strip leaves .text byte-identical
    cfg80211 import contract: 12 lines in cfg80211-provider.symvers (unchanged)
    deterministic: two consecutive builds from a clean stage are byte-identical

The sha above is the **fire-recipe identity**.  A rebuild in a different
directory can change `.note.gnu.build-id` (it hashes debug info that embeds
the build path) - build in `/tmp/wifi-he-caps-build` for the pinned sha.

## The patch (patches/0001-he-caps.patch)

Two files, 169 lines:

* `hi5622_cfg80211.c`:
  * the module parameter `he_caps` (default 0) with its contract description;
  * `hi5622_cfg11_he_apply()` - build the 6.18
    `struct ieee80211_sband_iftype_data` for one band **from the portable
    blob** (`hi5622_he_caps_fill()`) and install it with
    `_ieee80211_set_sband_iftype_data()`; the EHT/UHR fields 6.18 added stay
    zero (Wi-Fi 6 radio only); a failed allocation/blob refuses the band;
  * the call in `hi5622_cfg11_setup_one_band()` behind `if (hi5622_he_caps)`;
  * an init-time policy check: `he_caps>1` -> dev_err + `-EINVAL` (no wiphy,
    no AP; never guessed);
  * status sysfs reports `he_caps` (fire readback);
  * free the per-band iftype array on remove;
* `Kbuild`: add `hi5622_he_caps.o`.

Deliberately **not** in the patch: the capability values.  They live only in
`src/hi5622_he_caps.h` (the decoded contract macros); the driver consumes the
blob, and the build script refuses a driver that duplicates capability
literals (`rx_mcs_* = 0x...` or a static `phy_cap_info` array).  This is the
data contract: one source for kernel and host tests.

## Host checks

    ./tools/run-host-tests.sh

* part 1: `src/he_caps_selftest.c` (109 checks) - policy matrix
  (0/1/2/UINT_MAX), the per-band blob values (MAC 0x01, PHY 0x02 / 0x0c, MCS
  maps 0xfffa/0xffff, empty PPE, no 6 GHz), refusal cases (bad band, NULL,
  short buffer without writes), determinism/independence, and the exact
  HE Capabilities element bytes for both bands (T10);
* part 2: `tools/check-he-ie-selftest.py` (22 checks) - every verdict class
  of the offline on-air checker on synthetic radiotap beacons (PASS 2.4/5,
  absent IE, wrong caps, missing HE Operation, no beacon, band override,
  malformed pcap, exit codes).

Evidence: [evidence/host-tests.txt](evidence/host-tests.txt),
[evidence/build-artifact.txt](evidence/build-artifact.txt),
[evidence/base-pin.txt](evidence/base-pin.txt).

## The on-air checker (not part of the build)

`tools/check-he-ie.py` is the offline judge for the Lead's observer capture:
it parses a radiotap pcap, finds the `cudy-ap-test` beacon and compares the
HE Capabilities element (ext-id 35) and the HE Operation presence (ext-id 36)
against the same T10 contract.  Exit 0 PASS / 1 FAIL / 2 INCONCLUSIVE.
Usage and the fire gates are in `FIRE-RECIPE.md` G7.

## Safety and policy

* the parameter defaults to 0: every proven bench load line keeps working
  byte-identically unless `he_caps=1` is added;
* no device access, no firmware write, no frame builder change: the patch
  only changes what cfg80211/hostapd see (the wiphy HE advertisement) and the
  status line;
* no vendor object/firmware/calibration byte is committed; the decode
  addresses and derived capability values are in `TABLES.md`.
