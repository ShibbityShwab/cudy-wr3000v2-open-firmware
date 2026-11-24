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
-rw-r--r-- 1 ... 707912 ... /tmp/wifi-vap-start-kernel/hi5622_wifi.ko
6b61103756d710578a48122541417fac2f4f6f35e0b681d4998786074f0fef00
vsf_ symbols in module: 20
kernel-check: OK (zero warnings; module-only build; no image, no device)
```

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
