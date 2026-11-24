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
5. **`Kbuild`** - adds `vapstart_frames.o`; the two `vapstart_frames` sources are copied from
   `../src/` by `kernel-check.sh` (they are this lane's files, not the patch's).

## Bench use

```
insmod hi5622_wifi.ko allow_frames=8 frame_gap_ms=100 vap_start_first=1 \
       vap_start_mode=0 vap_start_module=0 h2d_send_budget=0
```

Then run the existing C2 ladder and trigger `iw dev wlan0 scan`; the scan op posts the VAP
sequence first. `recipes/vap_start_ladder.json` records the expected observables and the
stop rules. The patch is deliberately fail-closed: no AP_START capability is advertised (the
WAL AP-config body is still [U]), so `start_ap` keeps returning `-EOPNOTSUPP`.
