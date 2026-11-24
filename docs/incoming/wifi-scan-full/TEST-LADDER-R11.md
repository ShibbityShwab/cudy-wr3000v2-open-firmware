# TEST-LADDER-R11 - attended bench ladder for the refined g0 id10 frame (wifi-scan-full)

**Status:** design only.  This lane fired nothing (host-only).  The Lead owns
the fires; recovery door armed, UART read-only capture running, one fresh
golden boot per rung.  Image: self-contained image + the combined module built
with patch 0001 (BUILD-NOTES.md).  Nothing here replaces the fail-closed
default: `scan_band_cfg=0` transmits no g0 id10 frame.

Common load line (adjust budget to the image's allow_frames policy):

    insmod hi5622_wifi.ko fw_path=<PD> allow_frames=32 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        ap_start_mode=<as final module> scan_band_cfg=0

Stop conditions on EVERY rung (inherited from scan/dp lanes): page status word
!= 0x86807FEF, exception mask != 0, staging != 0, kernel oops/backtrace, missing
H1 dmesg line after a post, or firmware exception -> stop the sweep, power-cycle,
report.  A clean run ends with `iw dev wlan0 scan` printing DONE lines and the
board serving hostapd as before.

## Rungs

| rung | load delta | what it measures | PASS |
| --- | --- | --- | --- |
| R11.0 control | `scan_band_cfg=0 scan_req_batch=6` | no g0 id10 is sent; regression baseline | `H1 band-config` lines absent; batches 1/3..3/3 posted; 13/13 via batching; page golden; mask 0 |
| R11.1 vendor-shape smoke | `scan_band_cfg=1 scan_req_batch=0 scan_timeout_ms=12000` (defaults: chan=auto, slot=0, start=12, f10=13, mode=2, cmp=0, use2=1) | the 44-B refined frame is accepted exactly like the R4 36-B frame | H1 lines report **44 B** for both subtypes; >=7 channels or a clean host-timeout; page/mask clean; **no** exception |
| R11.2 subtype-2 semantics | R11.1 + `scan_band_cfg_use2=0` | isolates subtype 1 (companion clear removed: +0xFE/+0x106/+0x05 survive) | discriminates the pacing model: with subtype 2 removed, remaining effects must come from +0x104/+0x103/+0x10*; record channels/BSS for the model |
| R11.3 pacing sweep (core) | `scan_band_cfg=1 scan_band_cfg_use2=0 scan_req_batch=0 scan_timeout_ms=20000 scan_band_cfg_field10=<v>` one rung each: v = 0, 13, 25, 255 | payload+0x10 -> context+0x104 result-counter preset = the pacing lever | monotone channel count with v under a fixed window and no firmware stop => pacing model confirmed; FAIL if page/mask/exception on any v |
| R11.4 mode discriminator | `scan_band_cfg=1 scan_band_cfg_use2=0 scan_req_batch=0 scan_timeout_ms=24000 scan_band_cfg_field10=25 scan_band_cfg_mode=1` | mode=1 dense/passive branch (dwell/flush) combined with the counter preset | ~few channels but BSS-rich (>=10 BSS) => mode model confirmed; if 13/13 passes here, re-open the ceiling claim |
| R11.5 start/step rule | `scan_band_cfg_start=<v>` v = 0, 1, 12 with the rest of R11.1 | min rule: +0xFC is written only if 0 or v < current; +0xFD gets the high byte | behaviour inert across v (as R10c C2) => min rule confirmed; a channel-set shift falsifies it |
| R11.6 slot falsifier (decisive) | R11.1 with `scan_band_cfg_slot=1` and separately `=7` | the structural claim: slots 1..7 cannot extend coverage | identical channel count/results to slot=0 => multi-slot premise falsified (expected); any change => RE model wrong, capture and stop |
| R11.7 13/13 attempt (only if 11.3/11.4 move the count) | best 11.3/11.4 values + `scan_req_batch=0 scan_timeout_ms=30000` | can one 13-entry request finish? | DONE `chans=13/13` with `entries>0 iw_visible>0`; otherwise record the ceiling |

Record per rung: full dmesg (H1 lines, batch lines, DONE line, BSS check),
`iw dev wlan0 scan | grep -c BSS`, UART page/mask readout, boot id.  Rungs
11.1-11.2 are safety/regression; **11.3/11.6 are the scientific core**; 11.7 is
only worth a fire if 11.3 or 11.4 shows count movement.

## Decision after R11

- R11.1-11.3 pass + 11.6 slot-inert + no 13/13 => close the H1 line: ship
  `scan_band_cfg=0`; the documented ceiling is 6 without H1 and 7-8 with it;
  batching remains the 13/13 path.
- 13/13 on R11.4/11.7 => the pacing model is the lever: promote the winning
  values as a documented research-only recipe and re-run the final combined
  validation; do not change the fail-closed default.
- Any page/mask/exception => stop, snapshot calibration, report; the frame
  class stays off.
