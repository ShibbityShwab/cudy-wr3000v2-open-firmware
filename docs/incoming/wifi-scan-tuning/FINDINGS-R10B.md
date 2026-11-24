# R10b result and the H1 field sweep (task-133, 2026-11-27)

## Hardware result (R4 scan artifact 254,328 B sha256 713899a1, single request)

Load (bench): `allow_frames=32 h2d_send_budget=10` + defaults, then
`scan_band_cfg=1 scan_req_batch=0`.

- H1 band-config works: `H1 band-config subtype1 sent (chan=1 slot=0 start=12
  f10=13 mode=2 cmp=0, 36 B)` and subtype2 sent; no fault.
- One 13-entry request posted (window 4750 ms auto).
- **Channel results went past 6 for the first time: ch1..ch7 (2442 MHz), 7
  channels** - the scan context is re-armed by g0 id10, but only partially.
- Firmware then idled (~3.6 s) -> host-timeout abort, 7/13.
- **BSS decode proven on hardware**: the len-prefix layout decoded the g1 id15
  frames and cfg80211 accepted them: `scan BSS list check: entries=2
  iw_visible=2 (reported=4 inform_fail=0)`. Page golden, mask 0, no exception.

Interpretation: H1 is real (the vendor config does re-arm the firmware scan
context), and the BSS/inform/iw gate is satisfied. The remaining question is
which context field bounds the scanned channel count: with start=12, f10=13 the
firmware scans **7** channels. Two simple formulas fit 7 for the values used:
`(start>>1)+1 = 7` and `(f10>>1)+1 = 7`; if either is the real formula, the
value 25 (0x19) yields 13. Single-variable sweeps from the successful baseline
decide it.

## Ranked single-variable sweep (one reload / fresh boot per rung)

Common load line (bench `insmod`):

    fw_path=hi_wifi/FIRMWARE.PD4.bin allow_frames=32 frame_gap_ms=100 \
      h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 \
      vap_start_module=0 scan_band_cfg=1 scan_req_batch=0 \
      scan_timeout_ms=12000 <one delta below>

Baseline that produced 7 channels: chan=1 slot=0 start=12 f10=13 mode=2 cmp=0
use2=1.

| # | delta | what it tests | expected |
| - | --- | --- | --- |
| C1 | use2=0 | subtype-2 (undecoded) side effect on the context | 7 again = subtype2 harmless; different count = subtype2 matters |
| C2 | start=25 | count=(start>>1)+1 formula on +0xFC/+0x106 | **13/13** if true; a different count reveals the formula |
| C3 | field10=25 | count=(f10>>1)+1 formula on +0x104 | **13/13** if true |
| C4 | mode=1 | scan mode byte (context +0x103) | count/mode change |

Second round only if needed: `start=25 field10=25 use2=0` (both count fields),
`start=13 field10=13` (one-based end guess), `chan=0` (auto first channel),
`slot=1`, `cmp=1`.

Window: with the H1 config a single request may run past the auto 4750 ms; the
sweep sets `scan_timeout_ms=12000` (manual clamp [1000, 120000]) so the bound
never masks a slow full 13-channel scan. A compiled floor for
`scan_band_cfg != 0` (auto >= 8000 ms) is queued for R5 if the sweep succeeds.

## Ceiling statement (current, documented)

- Without H1 config: 6 channels/request, no re-arm (batch 2 never scans) - the
  R3 documented ceiling.
- With H1 config (start=12 f10=13 mode=2 use2=1): **7 channels in one request**
  and **real cfg80211 BSS entries visible to iw** (entries>0 iw_visible>0).
- Whether 13/13 is reachable is decided by C1-C4; if none changes the count,
  the honest firmware ceiling becomes "7+ channels per request with the current
  partially-decoded config, 6 without", and the shipped default stays
  `scan_band_cfg=0` (fail-closed) with batching.

