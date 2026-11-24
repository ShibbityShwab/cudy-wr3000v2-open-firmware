# R10c - measured H1 ceiling (task-133, 2026-11-27)

Sweep on the R4 scan artifact (254,328 B sha256 713899a1), single 13-entry
request, load: `allow_frames=32 h2d_send_budget=10 vap_start_first=1
vap_start_mode=1 vap_start_module=0 scan_band_cfg=1 scan_req_batch=0
scan_timeout_ms=12000`, one fresh golden boot per rung.  Baseline (from R10b):
chan=1 slot=0 start=12 f10=13 mode=2 cmp=0 use2=1 -> 7 channels (ch1-7), 4 BSS.

| rung | delta | channels scanned | BSS / results | DONE | reading |
| --- | --- | ---: | ---: | --- | --- |
| baseline | (none) | 7 (ch1-7) | 4 | chans=7/13 | H1 re-arm, partial |
| C1 | use2=0 | 7 | 4 | chans=7/13 | subtype-2 arm message is harmless (no side effect) |
| C2 | start=25 | 7 | 4 | chans=7/13 | +0xFC/+0x106 start field does NOT control the count |
| C3 | field10=25 | **8** (ch1-8) | 0 | chans=8/13 | +0x104 (field10) shifts the count/pace - one extra channel |
| C4 | mode=1 | **4**, dwelled ~3 s/ch | **14** | chans=4/13 | +0x103 mode=1 = dense/passive scan: trades channels for BSS richness (richest BSS yield of the sweep) |

Page 0x86807FEF golden, mask 0, staging 0, no firmware exception on every
rung - the g0 id10 config is **safe** on this firmware with these values.

## Measured ceiling

- **Without H1 config**: 6 channels per request, no re-arm for a second
  request (batch 2 accepted, never scanned) - the R3 ceiling.
- **With H1 config** (g0 id10 subtype1+2, start=12 f10=13 mode=2): **7
  channels**, one request, with decoded cfg80211 BSS (entries>0 iw_visible>0).
- field10=25: **8 channels**; mode=1: 4 dense channels with 14 BSS.
- **Full 13/13 was not reached** with any tested field value in a 12 s window.
  start/use2 are inert, field10 moves the count slowly (13 -> 7, 25 -> 8), and
  the firmware stops on its own (C3: no results after 8; a longer window only
  waits longer unless the stop is pace-limited).

These are protocol-unit semantics we cannot pin further from static evidence
alone: field10 is not a plain channel count (25 would need to give 13 if it
were `(x>>1)+1`, it gives 8), and the per-slot context array (band +0xEC +
slot*0x20, 8 slots) hints that full coverage would need MULTIPLE configured
slots (our driver only ever configures slot 0).  That is the one structurally
motivated remaining experiment, and it needs code (send subtype1 for slots
0..7), not a module-parameter combo - queued as future work, not fired here.

## Recommendation (close the lane)

1. **Shipped/production default stays fail-closed**: `scan_band_cfg=0`
   (nothing new is transmitted), batching `scan_req_batch=6` with the honest
   per-batch abort when the firmware does not re-arm (batch 1 completes 6
   channels; batch 2 is refused -> finish-abort with explicit H1 line).
2. **Max-coverage research run**: `scan_band_cfg=1 scan_req_batch=0
   scan_timeout_ms=12000` -> 7 channels in one request (8 with
   `scan_band_cfg_field10=25`) with decoded BSS.
3. **BSS-rich research run**: `scan_band_cfg=1 scan_req_batch=0
   scan_band_cfg_mode=1 scan_timeout_ms=24000` -> dense scan, ~4-6 channels
   but the richest BSS yield (C4 decoded 14 BSS in 12 s at ~3 s/channel).
4. **13/13 is not achievable on this firmware without the undecoded
   multi-slot/context semantics**: the documented ceiling is 6 channels without
   H1, 7-8 with the partially decoded H1 config.

## Remaining decider combos (top 2, param-only; fire only if the lead wants)

A. Dense-mode max coverage: `scan_band_cfg=1 scan_req_batch=0
   scan_timeout_ms=30000 scan_band_cfg_field10=25 scan_band_cfg_mode=1` -
   combines C3's extra channel with C4's dwell and a window long enough for
   ~9 dense channels; expected 8-10 channels with rich BSS, not 13.

B. Hard-stop confirmation: `scan_band_cfg=1 scan_req_batch=0
   scan_timeout_ms=20000 scan_band_cfg_field10=25` - same as C3 with a 20 s
   window.  If it still stops at 8, the firmware stop is a hard protocol limit
   (not time/pace) and the ceiling above is final.

If neither passes 8, close the lane: the headline results stand - H1 re-arm
extends past 6 (7-8), and the g1 id15 len-prefix decode + cfg80211 BSS/iw
visibility are hardware-proven (entries=2 iw_visible=2; 4-14 BSS per run).

