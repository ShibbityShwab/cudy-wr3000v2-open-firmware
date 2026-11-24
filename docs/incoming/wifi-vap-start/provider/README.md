# provider extension - VAP add/start capability for the wifi-scan-ap frame provider

`vapstart-provider.patch` is a unified diff against the committed
`../wifi-scan-ap/src` tree (`patch -p1` inside that directory). It is applied and
compile-verified by `../tools/kernel-check.sh` in a scratch copy.

## What it changes

1. **`hi5622_frames_api.h`**
   * caps `HI5622_FRAME_CAP_VAP_ADD (1u<<7)`, `HI5622_FRAME_CAP_VAP_START (1u<<8)`;
   * `struct hi5622_vap_start_params` (iftype, band, module, dst, seq);
   * callbacks `build_vap_add` / `build_vap_start` in `struct hi5622_frame_ops`.
2. **`hi5622_scan_logic.c`** - cap names, the caps-format bound, and callback/cap pairing
   validation for the two new caps.
3. **`hi5622_vapscan_provider.c`** - the two builder callbacks (map -> this lane's
   `vsf_build_*` library), the caps, and three module params:
   | param | default | meaning |
   | --- | ---: | --- |
   | `vap_start_first` | 0 | post the VAP start order before the first scan |
   | `vap_start_mode` | 0 | 0 = WAL group-2 vendor order, 1 = g0 id1 keys |
   | `vap_start_module` | 0 | module/VAP index (<= 0x12) |
4. **`hi5622_scan.c`** - `hi5622_scan_post_vap_start()` and its call in
   `hi5622_scan_op_scan`: VAP add -> VAP start -> scan. Any builder/post failure aborts the
   scan with the error and never sends the scan request. Off by default.
5. **`h2d_frames.c/h`** - the post-fire fix (2026-11-26): the inherited validator
   refused the WAL family because the firmware's static group-2 receive table has no id-0 slot
   ("firmware receive slot has no handler"), so `hi5622_frame_post()` never sent the VAP-add
   frame. `h2d_check_wal_cfg()` now validates that one decoded family structurally - envelope,
   WAL cfg-event id 1, command allowlist 0x0064/24, 0x0065/12, 0x00D0/16, 0x0127/156 - and
   refuses unknown commands/malformed lengths with the new `H2D_EWALCMD` error. Every other
   (group,id) keeps the generic non-NULL handler-slot rule. Tested by
   `../tools/validator-check.sh` (31/31 WAL cases + inherited 233/233).
6. **`hi5622_frame_post.c`** - the second-fire fix: the post path's own
   `h2d_fw_handler_lookup()` gate refused the WAL family ("no firmware handler for group/id")
   even after h2d_frames accepted it. It now recognises the decoded family through
   `h2d_frame_is_wal_cfg()` (firmware WAL/core module handles it; risk log prints
   `WAL_G2_CFG`). Every other frame keeps the static handler-slot gate.
7. **`Kbuild`** - adds `vapstart_frames.o`; the two `vapstart_frames` sources are copied from
   `../src/` by `kernel-check.sh` (they are this lane's files, not the patch's).

**Baseline:** generated against the `wifi-scan-ap/src` working tree of 2026-11-26 (which
already carries the scan-d2h decoder integration the bench module builds from); it also applies
to `git HEAD` + `../wifi-scan-d2h/patches/0001-hi5622-scan-d2h-decoder.patch` with line
offsets. Re-run `../tools/kernel-check.sh` after wifi-scan-ap changes.

**Compile evidence:** patched module 738,080 B, sha256
`9e26138de06c94371bfa776a8a511b3386bc852fca0020947729866f55b94bcc`, zero compiler/modpost
warnings, 20 `vsf_` symbols.

## Bench use

```
insmod hi5622_wifi.ko allow_frames=8 frame_gap_ms=100 vap_start_first=1 \
       vap_start_mode=0 vap_start_module=0 h2d_send_budget=0
```

Then run the existing C2 ladder and trigger `iw dev wlan0 scan`; the scan op posts the VAP
sequence first. `recipes/vap_start_ladder.json` records the expected observables and the
stop rules. The patch is deliberately fail-closed: no AP_START capability is advertised (the
WAL AP-config body is still [U]), so `start_ap` keeps returning `-EOPNOTSUPP`.
