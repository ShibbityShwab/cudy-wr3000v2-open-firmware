# BUILD-NOTES - the pinned base, the integration patch and the artifact pins

**Lane:** wifi-app-ie (task-234), 2026-11-26.  **HOST-ONLY**: built on the
local host against the project 6.18.60 kernel tree; no device contact, no
firmware write, no image, no flash.  The `.ko` is **local evidence only and
is never committed**; the committed outputs are the source, the patch, the
tools and the text evidence.

## 1. The base

The patch is authored and tested on the **R5-order base tree** (the F2/R5
lineage tip the sibling BA/TWT lanes also build on): the 65-parameter module
produced by `../wifi-band1-start-send-5/tools/build-r5-order.sh`.  Pins
(sha256, asserted by `tools/build-app-ie.sh`):

| file | sha256 |
| --- | --- |
| `Kbuild` | `1bb510853b305577d36e19a09507407f8f10d0a3d3531a5b4946a7d9b1fc141d` |
| `hi5622_scan.c` | `86d6c57ecc98d80906af1eeee1e5d6641c21a7ed5c1b2b53c70b85236b5b9373` |
| `hi5622_scan_logic.c` | `15c50d34fa7ed0f9bd5262e5b317c13a97228207dcc599673b72788e2b9bcfea` |
| `hi5622_scan_logic.h` | `6af774ef76b730c8f45d621bf338d12b041b96a3792be985df59ac5092a603e8` |
| `cfg80211-provider.symvers` | `bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7` |

The app-IE change is orthogonal to the BA/TWT/HE work: it adds one H2D step,
one plan entry and two module parameters.  Rebasing the patch onto the merged
**IMAGE #3** base / the TWT module is mechanical (same `hi5622_scan.c` ladder
region; the plan signature gains the `app_ie` argument in the same place) -
the integrator must re-run `tools/build-app-ie.sh` with `TREE_SRC` pointing at
that tree and re-derive the artifact pins; the **`.text` hash is the stable
functional identity**, the installed file hash is path/strip dependent (below).

## 2. The patch

`patches/0001-app-ie.patch` (sha256
`2c7f6621d49000934df74e608efa52dba93118366103fb96301b57e3c6126043`):

* adds `hi5622_app_ie.c/.h` (the portable module, identical to `src/`);
* `Kbuild`: one object line;
* `hi5622_scan.c`: `#include`, the two parameters + outcome statics, the
  fail-closed parameter gate before any frame, the cfg80211 0xDD collector,
  the ladder step between `vap_start_g0` and `band_start`, the dmesg lines,
  the `cfg80211_status` line, the collector buffer free;
* `hi5622_scan_logic.{c,h}`: the ordering step + plan signature + step name.

Verified: applied with `patch -p1` to a fresh copy of the pinned base it
reproduces the built tree file-for-file, and the build passes the pins below.
The patch is also what `tools/build-app-ie.sh` applies.

## 3. The build and the pins (what the fire checks)

Command: `tools/build-app-ie.sh` (full log `/tmp/app-ie-artifact/build.log`,
summary in `evidence/BUILD.txt`):

```
base copy -> patch -p1 -> make ARCH=arm CROSS_COMPILE=<OpenWrt 6.18.60 gcc> \
    -C <KDIR> M=/tmp/app-ie-build modules  -> 0 warnings, 0 errors
strip-kmod.sh (OpenWrt, NO_RENAME=1) -> the installed form
```

| artifact | size | sha256 |
| --- | ---: | --- |
| installed (`/tmp/app-ie-artifact/hi5622_wifi.ko`) | **368,348 B** | **`0c8390e8b498b668a066f4e7c74ad9fdafa7aa5586db22fb81313409cfcb0bdb`** |
| installed `.text` | 159,576 B | **`7867d4f0294bae925c9118a4e4360fd1b4815f6d57c9abea8318a9e1caa9a207`** |
| unstripped | 1,444,468 B | `5d553a43db70dd9682264e3443388ad1c58be5bc2b56148db106fd3ad688d156` |

Parameters: **67** (65 base + `app_ie`, `app_ie_type`); `modinfo -p` shows
both.  The installed sha is pinned for `WORK=/tmp/app-ie-build` because the
module embeds build-path strings - a package build from another path yields a
different file hash but the **same `.text` hash**; the fire recipe accepts
either check (see FIRE-RECIPE H0) and both are re-derived by the build script.

Lane source pins: `src/hi5622_app_ie.h`
`e0f3b39f9b15d410ed70a62a7e7977433d3454158457a7b9c7c4e0902929432c`,
`src/hi5622_app_ie.c`
`19c9cbf7d822a05eac919ac4bf660058b38b86d67b4a912feac0470be5a306ba`,
patch `2c7f6621d49000934df74e608efa52dba93118366103fb96301b57e3c6126043`, all
asserted by the build script.

The ladder step also carries a **read-only firmware slot read-back** (see
TABLES T4/T8): after the post the driver resolves the firmware VAP context and
reads `vap + 0x24c + 20*type` (`{ptr,len,cap}`) and logs it - diagnostic,
non-gating; it is the host-side witness the fire records.

## 4. Host tests

`tools/run-host-tests.sh` compiles `src/hi5622_app_ie.c +
src/app_ie_selftest.c` with the host `cc` (`-std=c99 -Wall -Wextra
-Werror`) and runs it: **55 checks, 0 failures** (`evidence/HOST-TESTS.txt`).
The first check rebuilds the captured stock frame byte for byte; the others
cover the geometry, the collector, the whole fail-closed matrix and the
cfg80211 composition model.

## 5. What is not here

No .ko, no firmware bytes, no vendor object, no calibration value, no MAC.
The vendor wifi.ko/FIRMWARE.bin used for the decode are local, gitignored
evidence; only decoded addresses, instruction behaviour and protocol bytes as
text are committed.
