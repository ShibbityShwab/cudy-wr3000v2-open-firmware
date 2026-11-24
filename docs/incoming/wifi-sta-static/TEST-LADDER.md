# TEST LADDER - wifi-sta-static

Host tests run anywhere; the bench rungs are **designed, not fired** by this
lane (host-only rule: no bench-device contact). Every bench rung is
fail-closed: page `0x86807FEF` golden + mask 0 + staging 0 is the health
witness; any deviation = stop, power-cycle, analyse offline.

## H - host rungs (no device)

| rung | command | pass criterion | evidence |
| --- | --- | --- | --- |
| H0 | `./tools/selftest.sh` | `ALL-SELFTESTS-PASS`: 2.4G/5G tables, HT/VHT words, connect bounds, WAL frames 140/48 B, mutation rejection | `evidence/selftest.txt` |
| H1 | `python3 tools/sta_re.py <vendor .ko>` | every summary assertion True; 108-byte body map reproduced | `evidence/sta-re-2.5.24.txt` |
| H2 | `./tools/build-sta-static.sh` | chain base builds, lane patch applies, zero warnings, artifact assertions, selftests | `evidence/build-verify.txt` |
| H3 | patch idempotence | `patch -p1 --dry-run < patches/0001-sta-static.patch` on a fresh chain = clean | build script does the real apply |

## B - bench rungs (Lead fires, one idea per boot)

Preconditions for every rung: fresh golden boot, PageCheck golden
(`0x86807FEF`), mask 0, staging 0, one module load, `allow_frames` set,
no prior aborted sequence.

| rung | module params | action | expected / pass | stop condition |
| --- | --- | --- | --- | --- |
| B0 | defaults + `allow_frames=16` | `insmod`, `iw phy`, `iw dev` | wiphy registers; capabilities show baseline HT20 (ht_mode=0) - control that the patch itself changed nothing | any register/ready failure |
| B1 | +`ht_mode=2` | `iw phy` | HT capabilities show `20/40 MHz`; 5 GHz wiphy additionally 160 MHz-capable when `vht_cap=1` | WARN splat or no wiphy |
| B2 | +`band_vendor=1` | `iw phy` | 2.4G lists ch1-14; 5G lists 29 channels incl. 4.9 GHz block (many disabled by regdb) | wiphy registration -EINVAL |
| B3 | +`sta_join=1` | `iw dev <sta> connect -w ssid test` (or wpa_supplicant) | dmesg: "WAL cfg-event opcode 0x123 frame built+validated (140 B ...) - dry run"; userspace gets -EOPNOTSUPP; **no frame on the wire** (allow_frames counter unchanged) | any frame posted in dry-run |
| B4 | +`sta_join=2`, `allow_frames=16` | a real `iw connect` to a test AP with known BSSID | dmesg: frame posted (140 B); page/mask/staging stay golden; optionally D2H event family changes | page != golden or mask != 0 -> power-cycle, document exception |
| B5 | B4 + a second boot | `iw disconnect` (or ifdown) | 48-B WAL opcode 0x00d0 frame posted; page golden | any mask/staging change |
| B6 | `sta_join=2` | repeat connect after disconnect | second connect frame posted; sequence increments; no stale-VAP style refusal | firmware exception |

Notes:
* B4's risk is the [U] host-buffer dereference contract of the connect body
  (section 3.3 of REPORT). It is a single controlled post, same class as the
  bench-proven AP-start WAL frames but with four host addresses the firmware
  may read - treat a mask change as the experiment's answer, not a regression.
* Insert a 5 GHz scan rung between B2 and B3: on the 5 GHz wiphy run
  `iw dev <5g-iface> scan` with `scan_band_cfg=0` first (the request entries
  already carry band=1); if the firmware accepts the 5 GHz request, the band
  config question of REPORT 4.3 is answered empirically. Keep `scan_band_cfg=1`
  for a separate rung (5 GHz channel ids are [U]).
* HT40 *use* (not advertisement) needs the firmware channel-width config; the
  ladder stops at advertisement (B1/B2). AP on-air 40 MHz is the AP lane's
  beacon/air ladder.
* After every rung: capture dmesg slice + sysfs status; record page/mask in the
  lab ledger. Any fire that posts a frame must be preceded by
  `/proc/net/hi5622`? n/a - use the module's `cfg80211_status` sysfs file.

## Pass/fail summary for this lane

* Host/compile lane: **PASS** (see evidence).
* Bench: **not fired** - the ladder above is the deliverable.
