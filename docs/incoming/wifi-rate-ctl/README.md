# wifi-rate-ctl - the vendor alg_cfg engine / rate-control command surface (RC-1)

**Lane:** wifi-rate-ctl (shared task task-179). **Status:** host deliverables
committed 2026-11-25; the bench ladder is prepared for the Lead (this lane
never contacts the device).  Recorded: decode 41/41 + logic 70/70 + frame
cross-check 144/144 PASS; patched module builds clean (471,580 B, 0 warnings).

## What this lane answers

RC-1 asks for the vendor's rate-control/alg engine: the alg cfg tables, the
hmac_config_alg path, the per-rate TX power tables, and the minimal config our
clean-room driver needs to enable the vendor's rate adaptation (vs the
default/static rate), with a rate-selection observable.

Answer (detail in REPORT.md):

* The firmware does the rate adaptation; the host surface is a text/args ->
  binary alg_cfg command engine: a 414-row process table, a 416-entry dispatch
  LUT (blob length + value field descriptors), and a family/per-id response
  dispatcher.  Every rate-control command (ids 401..422 and 1801..1809) and its
  wire value layout is decoded here [V].
* The minimal config is one config event: SET rate_mode = auto (cfg id 401,
  value byte 0) on the bench-proven g0 id2 / key 0x0101 carrier; the
  observable is the firmware's g1 id3 / syn 0x0101 config-sync answer plus a
  GET rate_mode readback, and optionally GET get_current_ration (id 421) for
  the selected tx/rx rate report.
* The vendor userspace never configures rate control (rootfs evidence); the
  firmware default is its own adaptation.  Fixed/static rates are the explicit
  hi_rate / legacy_rate blocks or rate_mode=fixed.

Calibration family 3502..3507 (already wire-proven) shows the same carrier;
the rate family is device-processed with the same wait-for-device flag
(0x01010000), so the response contract is identical.

## Directory

    REPORT.md          the decode + the minimal config + the observables
    TABLES.md          curated command/value/response tables
    FIRE-RECIPE.md     the attended R0..R3 ladder (Lead fires)
    BUILD-NOTES.md     the pinned patch chain and artifact identity
    patches/           the fail-closed driver patch
    src/               portable pure logic + host selftest
    tools/             the decoder, patch generator, test runner, build script
    evidence/          decode selftest, generated tables, host/build evidence

## Reproduce

    python3 tools/decode_alg_engine.py --selftest
    ./tools/make-patch.py --verify     # regenerate the driver patch (deterministic)
    ./tools/run-host-tests.sh          # decode selftest + C logic selftest
    ./tools/build-rate-ctl.sh          # apply patch to a staged copy + build
                                       # (host build; no device, no image)
    # recorded identities are in BUILD-NOTES.md and evidence/

The vendor module is read in place from
/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
(env HI5622_WIFI_KO overrides); it is never copied into the repository.

## Safety

Same rules as the rest of the project: no device contact from the lane, no
flash, no UART write, never CA 0x400392f0, never 0xCECE during boot, one
message per idea, default-off module parameter.  The patch only uses the
proven H2D config-event path (no new register writes).