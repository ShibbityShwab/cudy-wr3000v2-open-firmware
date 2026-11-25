# wifi-ax-he-caps - the HE capability advertisement (task-177, AX-1)

HOST-ONLY lane of the Cudy WR3000 v2.0 (Hi5622V100, 802.11ax 2x2 dual-band)
clean-room Wi-Fi 6 project.  This directory is the decode + fail-closed patch
+ host tests + fire recipe for **AX-1**: the wiphy's HE (802.11ax) capability
advertisement, and the beacon's HE Capabilities element that depends on it.

## What is here

| path | content |
| --- | --- |
| `REPORT.md` | findings: vendor wiphy HE caps, the hostapd gate, the firmware-beacon chain, the user_add HE block (AX-2 prerequisite), the patch design, limits |
| `TABLES.md` | T1-T11: addresses, decoded values, hostapd code references, exact expected on-air bytes |
| `FIRE-RECIPE.md` | the attended rung (2.4 GHz core, 5 GHz variant), exact PASS/FAIL gates - **the Lead fires** |
| `BUILD-NOTES.md` | the pinned base, the patch, the artifact contract, the host checks |
| `patches/0001-he-caps.patch` | the fail-closed patch against the 58-param vap5g module tree |
| `src/hi5622_he_caps.{c,h}` | portable decoded HE-cap blob + policy + HE Capabilities IE builder |
| `src/he_caps_selftest.c` | the host selftest |
| `tools/` | host test entry point, module build script, offline on-air HE-IE checker (+ its selftest) |
| `ladders/` | the two hostapd fixtures the fire recipe uses (2.4 GHz core, 5 GHz variant) |
| `evidence/` | curated host transcripts (build, artifact contract, tests, base pin) |

## The parameter

    he_caps=0   off (default, fail closed): no HE iftype_data; the bench
                module is behaviourally unchanged, hostapd refuses
                ieee80211ax=1 with "Driver does not support HE"
    he_caps=1   advertise the vendor-decoded HE caps for the AP iftype on
                the installed bands (2.4 GHz: 40 MHz; 5 GHz: 40/80/160 MHz;
                2 SS MCS0-9; no PPE, no 6 GHz)
    he_caps>1   refused at init (-EINVAL): no wiphy, no AP, no frame (never
                guessed)

## Status

Vendor decode, patch, host tests and host build are DONE (this directory,
committed):

* module artifact: stripped **356,564 B**, sha256
  `475975d32c9e56e2de1974d28ea983f1e0e7e36c4a0465ae4eeb7ea19755cdea`,
  **59 params** (`he_caps` default 0), 0 warnings, .text identical after
  strip; built on the pinned 58-param vap5g tree (base sha `13e7bf4c...`);
* host checks: 109 contract checks + 22 offline on-air checker checks, all
  PASS (`./tools/run-host-tests.sh`, no device);
* fire expectations: 2.4 GHz core rung — after `he_caps=1`, `iw phy` shows
  `HE Iftypes: AP` and hostapd accepts `ieee80211ax=1`; the on-air beacon
  carries the HE Capabilities element (T10 bytes) + HE Operation.

The fire is **not executed by this lane**: `FIRE-RECIPE.md` is the one-shot
rung the Lead runs on a fresh (power-cycled) chip with the recovery door
armed.  PASS = hostapd accepts HE + the on-air beacon carries the expected
HE Capabilities element; FAIL = stop, capture, power-cycle, no retry (the
recipe records which gate failed and what that falsifies).
