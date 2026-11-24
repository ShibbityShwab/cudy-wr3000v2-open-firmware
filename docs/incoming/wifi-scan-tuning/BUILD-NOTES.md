# BUILD-NOTES - wifi-scan-tuning module (task-133)

**Lane:** wifi-scan-tuning, host-only. **Date:** 2026-11-27.
**Diff base:** the bench-validated **combined AP+DP** source tree (ap-start v7 +
wifi-dp-air merge + scan-complete; the tree that produced the fired module
242,572 B sha256 `44c6cc12...`).  This lane changes four files
(`hi5622_scan.c`, `hi5622_scan.h`, `hi5622_scan_logic.c`,
`hi5622_scan_logic.h`) and one host selftest; nothing else.  The built `.ko` is
local evidence only and is NOT committed (no binary, vendor object, firmware
byte or calibration value is involved).

## Build line

    export BASE_TREE=/tmp/wifi-combined-build   # local copy of the fired tree
    ./tools/build.sh

Canonical environment (project tree):

    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

## Result (canonical run 2026-11-27, host-only)

The recording session executed every step of `tools/build.sh` individually
(host selftests, kernel module build, strip, metadata, request-dump compile)
and validated the patch round-trip; the wrapper was not invoked in one shot
because the host was under heavy parallel load.  `tools/build.sh` encodes the
same command lines and is the intended one-command re-run.

| item | value |
| --- | --- |
| module (with debug info) | 935,032 B, sha256 `aa9a6ce7da699e4da1d1010e605ceb524b3fdb52a33cfb3b8170e276d2d8c5ad` |
| module (stripped for deployment) | **246,688 B**, sha256 `05dd5f6ac83daf45df37e1e6761dde93c6f7ce9d7d4c03c81adf67f369e5244a` |
| ELF | 32-bit LSB relocatable, ARM, EABI5 |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| depends | cfg80211 |
| cfg80211 imports (U) | 9 (unchanged) |
| compiler/modpost warnings | 0 (`-Werror` tree) |
| host scan-tuning selftest | **37/37 PASS** (new) |
| host scan-complete selftest | **196/196 PASS** (inherited, window cadence updated) |
| host inherited scan/provider selftest | 290/290 PASS |
| host inherited cfg80211 regression | 454/454 PASS |
| host inherited H2D regression | 242/242 PASS |
| previous fired module (combined) | 242,572 B stripped sha256 `44c6cc12...` (base only; no kernel config change) |

Size/sha are build-path dependent (debug info records the build dir); the
stripped number above is the deployable figure.  The new code adds no external
symbol, no new `depends` and no new cfg80211 import.

## Module parameters (new in this lane)

| param | default | meaning |
| --- | ---: | --- |
| `scan_chan_time_ms` | 0 | per-channel cost for the auto window; 0 = compiled 250 ms (measured ~156 ms + margin); valid 50..2000, out-of-range ignored |
| `scan_req_batch` | **6** | channels per g0 id0xC request. 0 or >= n = one request (legacy); 1 = one request per channel; 6 = the measured firmware per-request completion |
| `scan_batch_gap_ms` | 300 | pause between chained requests (firmware scan-in-progress clear + late BSS flush), 0..5000 |

Changed semantics of existing parameters:

| param | change |
| --- | --- |
| `scan_timeout_ms` | still 0 = auto, but the auto term is per **request/batch** now: `clamp(1500 + n*scan_chan_time_ms, 1500, 120000)`; manual values are clamped to [1000, 120000] (the 45 s clamp is raised to 120 s) |
| `scan_dwell_ms` | unchanged (0 = vendor 60, 20..200 sweep) |

## Patch stack for the final combined module (task-133 handoff)

Verified against the fired combined AP+DP base tree, including the
image-polish conflict (both patches touch `hi5622_scan.c`):

1. base = bench-validated combined AP+DP tree (fired module 242,572 B sha
   `44c6cc12...`);
2. apply image-polish's module patch first (`scan_dwell_ms` default 60 lives
   in the same file);
3. apply `patches/0002-scan-tuning-on-polish.patch` (clean over the
   polish-applied tree).  `patches/0001-scan-tuning.patch` is the same lane
   delta over the bare base if image-polish changes are not taken.

The merged tree was host-tested: 37/37 scan-tuning + 196/196 scan-complete;
merged `hi5622_scan.c` has 5 scan params (dwell 60 from polish + the three new
ones).  stability lane built the final hardened module from this stack, but
the first bench R1 (fired with the earlier artifact) exposed a chain-advance
bug; this revision fixes it and rebuilds (see below).

## R1-follow-up rebuild (after the first bench R1, 2026-11-27)

The first artifact (246,688 B sha `05dd5f6a`) was fired: batch 1/3 started but
the scan closed `finish-ok` right after batch 1 (chain never advanced) and
`iw` showed no BSS lines although 8 were parsed.  Root cause + all fixes:
REPORT.md sec 9.  Summary: the work item fell through to the model tick after
arming the inter-batch gap, and the batch-local coverage finished the scan.
Fixes: chain advance blocks the tick while the next request is pending;
coverage is accumulated over all batches (`chans=13/13 batches=3/3`); settle
also follows the last BSS event; kernel-side cfg80211 BSS-list readback
(`entries`/`iw_visible`) diagnoses iw visibility (`cfg80211_bss_iter` is now
the 10th import; `cfg80211-provider.symvers` gained one line and is part of
the patch stack).

| item | value |
| --- | --- |
| artifact for the R-ladder (base+polish+scan, final-stack parity) | **247,776 B** sha256 `613989fff96028a1678b95e92208cb8a34416ac9aec2e7cffb7c77c5530d5419` at `/tmp/wifi-scan-tuning/hi5622_wifi.ko` |
| bare combined-base variant (no polish defaults) | 247,424 B sha256 `fd2082ab2687dfec4015b619ca3ac2275a20dee86b9508fdd29ed2d2229d3823` at `/tmp/wifi-scan-tuning/hi5622_wifi.scan-only.ko` |
| build | 0 warnings, vermagic 6.18.60 SMP ARMv7, depends cfg80211, 3 new params present, 10 cfg80211 imports |
| patches | regenerated: `0001` (bare base) and `0002` (polish-applied) now carry **all five files** (hi5622_scan.c/.h, hi5622_scan_logic.c/.h, cfg80211-provider.symvers); both verified with `patch --dry-run` (fixes the stability lane's patch-gap report - the earlier 0002 lost files to a `set -e` diff loop) |
| host tests after fixes | 37/37 scan-tuning, 196/196 scan-complete, 290/290 scan-logic PASS, plus the new 7/7 relaxed-decoder selftest |

## R2 revision (after the second R1 fire: completion-event + BSS decode)

The second R1 fire (artifact 247,776 B sha 613989ff) showed another failure
mode: a firmware completion event arrived after batch 1 (complete=1, status not
1) and was treated as scan-global -> finish-abort[complete-failed]; and all 6
g1 id15 BSS frames failed decode (parse=6, inform_fail=0, BSS list entries=0).
Fixes: completion events are batch-local while more batches remain; on the
final batch the channel-coverage rule wins over a partial-request status; the
BSS decoder gained a relaxed alignment probe (frame offset 0..32 x trailer
len 0/4/8/12/16/20/24, reported as layout "relaxed") plus a ratelimited
"scan BSS decode gap: len=.. first=.." hexdump on total failure so the true
framing is visible in dmesg.

| item | value |
| --- | --- |
| artifact for the R-ladder (base+polish+scan, final-stack parity) | **248,668 B** sha256 `ca81660f03bc06eecc526cafbfa9b0e7d862a8ff878dbbeaa2f29c2c5b235a98` at `/tmp/wifi-scan-tuning/hi5622_wifi.ko` |
| bare combined-base variant | 248,312 B sha256 `118c3b3180bb8d1a339304a171b008640f56830332548a4c880d5d5b181bce97` at `/tmp/wifi-scan-tuning/hi5622_wifi.scan-only.ko` |
| build | 0 warnings, vermagic 6.18.60 SMP ARMv7, depends cfg80211, 3 new params present, 10 cfg80211 imports |
| patches | `0001`/`0002` regenerated with **seven** files (hi5622_scan.c/.h, hi5622_scan_logic.c/.h, hi5622_scan_d2h.c/.h, cfg80211-provider.symvers); both verified with `patch --dry-run` |
| host tests | 37/37 scan-tuning, 196/196 scan-complete, 290/290 scan-logic, 7/7 relaxed-decoder selftest PASS |

## Tree quirks

Inherited, unchanged: cfg80211-provider.symvers for the 9 modpost symbols,
heap-allocated h2d_frame / vs_frame, shared KDIR must be idle during a world
build.  New: the lane `src/` is a **four-file delta**, not a full tree - build.sh
copies `BASE_TREE` and overlays it, which is also how the patch was made.

## Final combined module pin (R2 handed to the stability lane)

Stability rebuilt the hardened combined module with this lane's R2 patch
(opensource commit 8b95aa1) and it was independently re-verified from the
artifact before handing it to the lead to fire:

| item | value |
| --- | --- |
| final combined R2 (stripped) | **263,740 B** sha256 79bfad6e5f80c8ef053040c55ccd945fe1824ee380c24e97c3e20a204a40d768 |
| final combined R2 (unstripped) | 972,208 B sha256 f9f916b1e19ee23abc65b6888bf10e61d0be15c8e659eb38b4716394054e321e |
| path | /tmp/wifi-stability-artifact/hi5622_wifi.hardened.ko (unstripped under /tmp/wifi-stability-build/combined/) |
| chain | base 44c6cc12 -> polished d145a205 -> dp-tuning 175a4617 -> scan-tuning R2 (0002, 7 files) -> stability hardening |
| verified | vermagic 6.18.60 SMP ARMv7, depends cfg80211, 0 warnings; scan_chan_time_ms/scan_req_batch/scan_batch_gap_ms params; 10 cfg80211 imports incl cfg80211_bss_iter; R2 strings ('split into %u g0 id0xC requests', 'scan BSS decode gap', 'relaxed') present |

The lead fires R1 with this artifact (defaults; hcc_trace=0). Expected: 3
batch lines, 13 channel results, no complete-failed abort after a non-final
batch, `scan BSS list check: entries>0 iw_visible>0`, DONE
`finish-ok[...], channel=13, chans=13/13 batches=3/3`, iw >=1 BSS.

## R3 revision (after the third R1 fire: batch window + real g1 id15 framing)

Third R1 fire: the chain advanced to batch 2/3 (batch-local completion works),
then the per-request window expired during batch 2 with zero new channel
results, and the BSS decoder still failed - but the new decode-gap hexdumps
captured the real g1 id15 framing.

Framing decoded from the captures: payload = [72-byte vendor scan-result
header][802.11 frame][20-byte trailer], where the u16 at payload[0] IS the
802.11 frame length: 457 - 20 - 365 = 72 and 497 - 20 - 405 = 72 for both
bench lengths.  The parser now derives frame offset = len - 20 - u16(payload[0])
(layout reported as "len-prefix"), keeps the fixed/relaxed probes as
fallbacks, and keeps the decode-gap hexdump.  Chain: default inter-batch gap
raised 300 -> 800 ms, every batch after the first gets +2500 ms of request
window (batch 2/3: ~5.5 s), and a non-first request that yields no channel
result within 2000 ms is reposted once (recovery from a silently refused
request while the firmware still holds scan-in-progress).

| item | value |
| --- | --- |
| artifact for the R-ladder (base+polish+scan, final-stack parity) | **249,580 B** sha256 1710bf113a7e11ee40bbd9b7bbc05f196120dd91859f85c52816f93dc924c6aa at /tmp/wifi-scan-tuning/hi5622_wifi.ko |
| bare combined-base variant | 249,228 B sha256 8e6f3b176edb68335e3c5b50d04c9bce088ce6071536cf6f55080faefacfc849 |
| build | 0 warnings, vermagic 6.18.60 SMP ARMv7, depends cfg80211, 3 scan params, 10 imports |
| host tests | 12/12 decoder (incl. bench framing), 37/37 tuning, 196/196 scan-complete, 290/290 scan-logic |
| patches | 0001/0002 regenerated (7 files), patch --dry-run clean |
