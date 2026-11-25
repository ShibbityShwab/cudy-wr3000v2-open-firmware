# BUILD-NOTES - lane wifi-desc-table-send (task-188)

HOST-ONLY build.  No device access, no firmware write, no image, no flash.
The `.ko` is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts).

## Chain

    ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh
      = /tmp/beacon5g-build + /tmp/beacon5g-artifact/hi5622_wifi.ko
        356,600 B sha256 bc1cc0ea...a1bbb, 59 params (beacon5g gate)
    ../wifi-r1-band-diff/tools/build-r1-band.sh
      = /tmp/r1-band-build + /tmp/r1-band-artifact/hi5622_wifi.ko
        356,612 B sha256 e8b17f72...4abd4, 59 params (the CW-0/R1 module;
        the r1-band one-field VAP-start band fix is applied here)
    this lane patches/0001-desc-table-send.patch
      = the H2D 0x00F7 descriptor-table send + the fail-closed desc_table
        parameter (see REPORT.md sec 2)

`tools/build-desc-table-send.sh` automates the last step; it rebuilds the
base chain (beacon then r1-band) only when `/tmp/r1-band-build` is absent,
and pins the base between steps:

| pin | value |
| --- | --- |
| beacon5g base provider | sha256 `bc7e3ede24eaa21bc9ecf01dfb82687898a4e5fc378e77926f77619e200ce828` (the r1-band script's own pin) |
| r1-band PATCHED provider | sha256 `c306e75fb4d640812278dc1ccdd59c8c197ba6e3f42da2b7f5ff98010d8b79ab` |
| r1-band base artifact | `/tmp/r1-band-artifact/hi5622_wifi.ko`, 356,612 B sha256 `e8b17f7238afce459db61a9ca65b9a0a1336ce232c933c65990d444350e4abd4`, 59 params |

The r1-band build script pins the *unpatched beacon* provider bc7e3ede; the
patched tree's provider is c306e75f (same file + the one-field fix) - both are
checked when the beacon tree is present.

## Artifact (host build, not fired, never committed)

| field | value |
| --- | --- |
| stripped artifact | /tmp/desctable-artifact/hi5622_wifi.ko, **360,448 B** |
| sha256 | `aac6177ce84c972dafff67061951c2fe28e31a330f636c7a185c1511dfe800e3` |
| unstripped | 1,370,144 B |
| .text sha256 | `6cf0cabd5d1b15b085f06d91f1522805408e8f5033020adaf1375ac4f8b24149` |
| params | 60 (r1-band 59 + `desc_table`) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| warnings | 0; debug-strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines (unchanged) |
| size delta | +3,836 B vs the 356,612 B r1-band base |

`desc_table` is a read-only (0444) uint parameter, default **0** (off =
current behaviour); the built module's .modinfo carries
`parmtype=desc_table:uint` (the 6.18 loader drops MODULE_PARM_DESC strings,
so the description is checked in the patched source, not in the artifact).

## Contract checks implemented by the build script

1. the beacon5g base provider (when present) equals the r1-band lane's pin;
2. the r1-band patched provider equals `c306e75f...` and the base artifact
   equals 356,612 B / `e8b17f72...` (refuse a drifted base);
3. the patch applies to the pinned tree with no fuzz/conflict markers;
4. `src/hi5622_desc_table.{c,h}` are byte-identical to the patched driver
   sources (source-drift guard);
5. module build 0 warnings/errors/undefined symbols;
6. debug-strip does not change the .text;
7. artifact contract: 60 params incl `desc_table`, `desc_table` declared as
   read-only uint with a MODULE_PARM_DESC in the patched source, the 0x00F7
   dmesg/status feature strings, 12 cfg80211 symvers.

## Host tests

    ./tools/run-host-tests.sh

Compiles the portable model (src/) and the real patched ordering model
(hi5622_scan_logic.c from the tree) with a plain C99 host compiler and runs
`tools/desc_table_send_test.py` (independent frame re-derivation + source
order/contract checks).  Result: 108 checks, 0 failures
(evidence/host-tests.txt).  No device contacted.

## Reproduce

    ./tools/build-desc-table-send.sh
    ./tools/run-host-tests.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with KDIR_OVERRIDE / STAGING_OVERRIDE.
