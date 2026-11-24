# Integration patch - scan-complete lane (task-123)

`0001-scan-complete.patch` is a unified diff (`patch -p1` from the lane root)
over the **bench-validated base tree**:

* `wifi-scan-ap/src` at opensource commit `231a39d` (the module fired on
  2026-11-26, built from that tree with the scan-d2h decoder integration), plus
* the **wifi-vap-start provider patch**
  (`docs/incoming/wifi-vap-start/provider/vapstart-provider.patch`, task-117,
  caps 0x19f, module 738,112 B sha256 `e09dc697...`), plus
* `vapstart_frames.c/.h` copied from `docs/incoming/wifi-vap-start/src/`
  (the provider patch's `Kbuild` reference; they are not part of the diff).

Apply:

    cd docs/incoming/wifi-scan-ap/src
    patch -p1 < ../../wifi-vap-start/provider/vapstart-provider.patch
    cp ../../wifi-vap-start/src/vapstart_frames.[ch] .
    cd ../..
    patch -p1 -d wifi-scan-ap < ../../wifi-scan-complete/patches/0001-scan-complete.patch

The patch changes only `src/` and `tools/` (no docs): docs live in this
directory. Applying it to the base tree and building must reproduce
`tools/build.sh` PASS with the artifact recorded in BUILD-NOTES.md (module
size and sha256 are build-path dependent through debug info; behaviour is
not).

Regeneration (from this lane's tree): the patch is `diff -ruN` between the
base tree above (with pristine `wifi-scan-ap/tools`) and this lane's
`src/` + `tools/`, with build products removed. See REPORT.md section 5 for
the per-file change table.
