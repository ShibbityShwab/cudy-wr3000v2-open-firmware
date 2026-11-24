# wifi-scan-full - the g0 id10 per-slot context array, full RE and refined builder

**Lane:** wifi-scan-full (shared task-138), host-only clean-room static analysis.
**Date:** 2026-11-24.  **Base:** wifi-scan-tuning R4 (task-133) + the
bench-validated combined/hardened tree.  **No bench contact by this lane;**
the Lead fires the ladder.  **No vendor binary/firmware/calibration byte is
committed** - the analysis tool reads the local firmware in place and prints
addresses/annotations only.

## What this lane answers

R4/H1 showed that one g0 id10 subtype-1 frame re-arms the firmware scan
context (6 -> 7-8 channels) but full 13/13 was not reached; the open question
was whether programming the per-slot context array (8 slots, stride 0x20)
could make ONE 13-entry scan request cover all 13 channels.

**Answer:** no.  This lane decodes the complete message, the per-slot layout
and the scan-engine consumers, and shows that the message is the vendor host's
ADDBA-response/DELBA relay; it can alias the scan context's **pacing** fields
(slot 0 only) but has no payload field that writes the channel count/end byte,
and the scan engine never selects slots 1..7.  The shipped ceiling (6 without
H1, 7-8 with H1) is structural for this message.  The R4 H1 effect is
explained as a result-counter preset (pacing), matching the measured sweep.

## Deliverables

| path | content |
| --- | --- |
| REPORT.md | full decode: dispatch correction, every payload field, per-slot layout, scan-engine consumption, vendor cross-check, pacing model, limits |
| src/hi5622_band_cfg.{c,h} | refined builder (vendor 44 B wire shape, decoded fields, subtype-2 flag at +0x14, fail-closed) |
| patches/0001-scan-full-vendor-shape-bandcfg.patch | delta over the R4 tree; applies cleanly to the R4 and hardened trees |
| tools/scan_full_selftest.c | 90/90 host checks |
| tools/fw_scan_full_re.py | 65/65 static-evidence checks (regenerates every CA cited; optional vendor .ko cross-check) |
| tools/build.sh | one-command host + module verification |
| TEST-LADDER-R11.md | attended ladder (Lead fires) with falsifiers and stop conditions |
| BUILD-NOTES.md | build line, patch stack, results |
| evidence/ | captured host-test/build/RE outputs |

## Regeneration

    # static evidence (65/65 PASS): firmware is read in place, never copied
    .provision-venv/bin/python tools/fw_scan_full_re.py \
        --ko /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko

    # host selftest (90/90 PASS)
    cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I src \
        -o /tmp/scan_full_selftest tools/scan_full_selftest.c src/hi5622_band_cfg.c
    /tmp/scan_full_selftest

    # full verification (host tests + 6.18.60 module build, 0 warnings)
    BASE_TREE=/tmp/wifi-stability-patchcheck ./tools/build.sh

Pins: firmware sha256 `7fc87e2051e80b5e...`; vendor wifi.ko 2.5.24
`de78ec07e46e70ce...`; R4 base = wifi-scan-tuning src + 0002 patch.

## Safety / rules honoured

- host-only: no device, UART, network or image access; `scan_band_cfg`
  default remains 0 (fail-closed, nothing transmitted).
- no vendor bytes in tracked text: only addresses, ids and original code.
- the only new wire bytes are the vendor-shaped g0 id10 frame behind the
  existing fail-closed parameter; the R11 ladder is attended (Lead) with the
  page/mask/exception stop conditions inherited from the scan lanes.
