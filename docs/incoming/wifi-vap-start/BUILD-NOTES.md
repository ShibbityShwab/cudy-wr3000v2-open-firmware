# BUILD-NOTES - wifi-vap-start

Host-only build/verification notes. No image, no device, no vendor artifact is committed.

## Environment

```
repo:    /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
lane:    opensource/docs/incoming/wifi-vap-start
python:  .provision-venv/bin/python   (pyelftools for the vendor symbol probes)
host cc: cc
kernel:  /home/shibbityshwab/openwrt-lab-upstream-target/build_dir/.../linux-6.18.60
         (fallback: /home/shibbityshwab/openwrt-lab-selectorfix-img/...)
```

## 1. Host build + selftest (2026-11-26)

```
$ ./tools/build.sh
== compile (C99 -Wall -Wextra -Werror -pedantic) ==
== selftest ==

61/61 checks passed
RESULT: PASS
== emit bench frames ==
vap_add_g0id1_key0x64 total=48: 000100003000010000005a5a...
vap_start_g0id1_key0x65 total=40: 000100002800010000005a5a...
sta_join_start_g0id0x10 total=28: 000100001c00100000005a5a...
sta_join_params_g0id0xF total=76: 000100004c000f0000005a5a...
wal_vap_add_g2_cmd0x64 total=56: 020100003800000000005a5a...
wal_vap_start_g2_cmd0x65 total=44: 020100002c00000000005a5a...
61/61 checks passed
RESULT: PASS
build: OK (/tmp/wifi-vap-start-build/vapstart_selftest)
```

## 2. Static re-verification against the local vendor blobs

```
$ .provision-venv/bin/python tools/vap_start_re.py --selftest
VERIFY: 56 probes/symbols PASS, 0 FAIL
```

Inputs (gitignored, read in place): local FIRMWARE.bin 928,920 B sha256 7fc87e20...311b and
local hi5622v100_wifi.ko 3,564,728 B sha256 de78ec07...fd9521. The tool prints derived output
only.

## 3. Kernel-context compile (module-only)

```
$ ./tools/kernel-check.sh
== kernel-context compile result
-rw-r--r-- 1 ... 738112 ... /tmp/wifi-vap-start-kernel/hi5622_wifi.ko
e09dc697bd305c599c26712f60994c8e86cf127e00cfe329002e9979067dc4bf
vsf_ symbols in module: 20
kernel-check: OK (zero warnings; module-only build; no image, no device)
```

(The pre-validator-fix build recorded by the 2026-11-26 bench fire was 707,912 B / sha256
`6b611037...`; the current build adds the h2d_frames WAL family validation, the scan-ap
working-tree scan-d2h decoder integration and this lane's provider code.)

The check copies the committed `../wifi-scan-ap/src` tree to `/tmp/wifi-vap-start-kernel`
(never modifies another lane), copies `src/vapstart_frames.{c,h}`, applies
`provider/vapstart-provider.patch`, and builds with `make -C $KDIR M=$WORK modules`.
`.ko` is local evidence only.

## 4. Generated recipes

```
$ .provision-venv/bin/python tools/gen_frames_md.py
wrote .../recipes/frames.md
$ .provision-venv/bin/python tools/gen_frames_md.py --check
recipes/frames.md is current
```

## 5. Provider extension patch

`provider/vapstart-provider.patch` is a unified diff against the committed wifi-scan-ap `src`
tree. It adds:

* `HI5622_FRAME_CAP_VAP_ADD` / `HI5622_FRAME_CAP_VAP_START`, the contract struct
  `struct hi5622_vap_start_params` and the two builder callbacks in `hi5622_frames_api.h`;
* cap names, `caps_format` bound and callback pairing in `hi5622_scan_logic.c`;
* two provider callbacks plus module params `vap_start_first` (0 = off default),
  `vap_start_mode` (0 = WAL group-2 vendor order, 1 = g0 id1 keys) and `vap_start_module`
  in `hi5622_vapscan_provider.c`;
* the vendor order VAP-add -> VAP-start -> scan in `hi5622_scan_op_scan` (fail-closed: a
  start failure aborts the scan and the scan message is not sent);
* `vapstart_frames.o` in `Kbuild`.

Caveat: the patch targets the wifi-scan-ap src snapshot committed at lane time; re-run
`tools/kernel-check.sh` after that lane changes. It is an integration proposal, not a claim
that cfg80211 AP/STA support is complete (the WAL AP-config body is still gap G1).

## 6. Commit discipline

Only files under `opensource/docs/incoming/wifi-vap-start/` were added. No vendor binary,
object code, firmware byte, calibration value or secret is committed; no file outside
`opensource/` was touched.

## 7. Post-fire validator fix (2026-11-26)

The first bench fire of the provider module was stopped inside `hi5622_frame_post()`: the
56-byte WAL VAP-add frame failed `h2d_frame_decode(..., H2D_VF_HANDLER)` with
"firmware receive slot has no handler", because the firmware's static group-2 receive table
has no id-0 slot (the WAL family carries its command in the payload). The provider patch now
contains `h2d_check_wal_cfg()` + `H2D_EWALCMD` (allowed commands 0x0064/24,
0x0065/12, 0x00D0/16, 0x0127/156; unknown/malformed refused).

New verification step:

```
$ ./tools/validator-check.sh
37/37 checks passed            # WAL family + predicate accepted, mutations refused
SUMMARY: 233 passed, 0 failed  # inherited wifi-h2d-frames selftest on the patched library
validator-check: OK
```

Patch baseline: generated against the `wifi-scan-ap/src` working tree of 2026-11-26 (the tree
that carries the scan-d2h decoder integration the bench module is built from). It also applies
to `git HEAD` + `../wifi-scan-d2h/patches/0001-hi5622-scan-d2h-decoder.patch` with line
offsets (dry-run checked).

## 8. Second gate on the frame-post path (second fire, 2026-11-26)

The rebuilt 737,504 B module passed the h2d_frames validator but was then refused inside
`hi5622_frame_post()` by its own `h2d_fw_handler_lookup()` check ("no firmware handler for
group/id", -EOPNOTSUPP). That gate assumes a static HC handler-table slot, which the WAL
family does not have. The patch now adds `h2d_frame_is_wal_cfg()` (a predicate that only passes
a frame the structural family check already accepted) and the post path treats the WAL family
as handled by the firmware WAL module - every other id keeps the slot rule and unknown WAL
commands are still rejected before the gate.

Rebuilt module: 738,112 B sha256
`e09dc697bd305c599c26712f60994c8e86cf127e00cfe329002e9979067dc4bf`, zero warnings.
`tools/validator-check.sh` is now 37/37 + inherited 233/233.

## 9. Third fire + mode-1 rung (2026-11-26)

The 738,080 B module put all five frames on the wire (user_add, notify, WAL vap_add 56 B,
WAL vap_start 44 B, scan_req 520 B - all CONSUMED, page golden, mask 0), but the scan stayed
empty: the WAL layer does not populate the hCC module context, and the scan handler's first gate
is module0+0x4B8. The g0-mode VAP add now uses parent module = the module being created (same
self-parent pattern as the bench-validated C2 frame) and the mode-1 module was rebuilt:
**738,112 B sha256
`e09dc697bd305c599c26712f60994c8e86cf127e00cfe329002e9979067dc4bf`**, zero warnings;
validator-check 37/37 + inherited 233/233.

Mode-1 bench params: allow_frames=12 vap_start_first=1 vap_start_mode=1 vap_start_module=0
frame_gap_ms=100. Expected dmesg after the scan op: vap_add g0 id1 total 48 B CONSUMED,
vap_start g0 id1 total 40 B CONSUMED, then scan_req; decisive readback ctx0+0x190
(BAR0 0x7D28E8), ctx0+0x4B0 (0x7D2C08), ctx0+0x4B8 (0x7D2C10) non-zero (see TABLES T18).
