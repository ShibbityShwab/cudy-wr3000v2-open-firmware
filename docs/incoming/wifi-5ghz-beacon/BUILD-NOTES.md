# BUILD-NOTES - lane wifi-5ghz-beacon (task-175)

## Chain

    ../wifi-5ghz-bringup/tools/build-5ghz-ready.sh
      = association tree + sibling mgmt carrier + band5g patches (57 params,
        stripped sha256 13366151...892d53)
    ../wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh
      = the fail-closed vap5g vendor-order patch (58 params, stripped sha256
        13e7bf4c...2cc1abf = the E2 module under bench test)
    this lane patches/0001-beacon5g-gate.patch
      = corrected module-table readback + the fail-closed band-1 channel
        activation wait (ap_chan_wait_ms, default 0) + the read-only gate pack

`tools/build-beacon5g-ready.sh` automates all three (it runs the vapmsg5g
builder when the base tree is absent) and pins `BASE_ART` to the E2 sha.

## Artifact (host build, not fired, never committed)

| field | value |
| --- | --- |
| stripped artifact | /tmp/beacon5g-artifact/hi5622_wifi.ko, **356,600 B** |
| sha256 | `bc1cc0ead2c5b98a35eea875c353dc7e5bcf2e39b61ba9427408b714ea9a1bbb` |
| unstripped | 1,357,356 B |
| .text sha256 | `80ca20e6d2a06660af1361eef8a982e334da9f61502c6652899b3ed116b6273b` |
| params | 59 (E2 58 + `ap_chan_wait_ms`) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| warnings | 0; strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines (unchanged) |

Size delta vs E2: +2,948 B (planner, wait/poll, gate pack, corrected comments).

## Contract checks implemented by the build script

1. the E2 base artifact sha256 equals the pinned `13e7bf4c...2cc1abf`
   (refuses a drifted base chain);
2. the patch applies to the E2 tree with no fuzz/conflict markers;
3. `src/hi5622_beacon5g.{c,h}` are byte-identical to the patched driver
   sources (source-drift guard);
4. module build 0 warnings/errors/undefined symbols;
5. debug-strip does not change the .text;
6. artifact contract: 59 params incl `ap_chan_wait_ms`, the fail-closed
   feature strings, 12 cfg80211 symvers.

## Reproduce

    KDIR_OVERRIDE=... STAGING_OVERRIDE=... ./tools/build-beacon5g-ready.sh
    ./tools/run-host-tests.sh
    python3 tools/beacon5g_re.py --wifi-ko /path/to/hi5622v100_wifi.ko

The vendor modules are local evidence only (rootfs teardowns / recovery
extract). They are never copied into this directory or any commit.
