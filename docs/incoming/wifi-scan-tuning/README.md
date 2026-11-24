# wifi-scan-tuning - full 13-channel scan with tuned pace/batching (task-133)

Host-only deliverable of the **wifi-scan-tuning** lane (shared task **task-133**,
builds on wifi-scan-complete / task-123).  It diagnoses the measured scan
behaviour of the self-contained image + combined AP/DP module and tunes the
driver so one `iw dev wlan0 scan` can cover all **13 requested 2.4 GHz
channels** instead of the 6 the firmware completed.

**Baseline (hardware-measured, 2026-11-27, self-contained image ae5e28bf,
combined module 242,572 B sha 44c6cc12):** golden boot, `vap_start` mode-1,
g0 id0xC request posted (520 B, count field = **13**), then **six** g1 id17
channel results in **0.94 s** (slots 0..5 -> channels 1..6, ~156 ms/channel)
and *no frame for the remaining ~14 s*; the host window closed with
`finish-abort[host-timeout], chans=6/13`.  The old `3000+900*n` ms window
(14.7 s for 13 channels) was therefore wrong in both directions: too long for
the observed cadence, and sized as if the firmware would report every channel.

**What this lane delivers**

1. **Diagnosis** (REPORT.md sec 2-4): the exact 520-B request our provider
   builds (all 13 channel entries + count=13 confirmed, field map and firmware
   read offsets below), the firmware scan engine's parsing (CA 0x6494C ->
   CA 0x644A4 -> per-channel scheduler CA 0x63CB8 -> g1 id17 emitter
   CA 0x641B4) and the three candidate explanations for the 6-channel stop,
   ranked with the discriminator each one needs on the bench.
2. **Tuning** (REPORT.md sec 5): retuned auto window from the measured
   cadence (`1500 + 250*n` ms, clamp raised from 45 s to 120 s), three new
   module parameters (`scan_chan_time_ms`, `scan_req_batch`,
   `scan_batch_gap_ms`) and **chained multi-request scanning** - the host
   splits one cfg80211 scan into ceil(n/batch) g0 id0xC requests with the
   compiled default `scan_req_batch=6`, so 13 channels = 3 requests.  Only the
   channel *list* is divided between requests; every other wire field is
   unchanged.
3. **Verification**: device-free host tests (37 new checks + 196/196 inherited
   scan-complete + 290/290 inherited scan logic + 454/454 cfg80211 + 242/242
   H2D), clean kernel-module build against the project 6.18.60 tree
   (246,688 B stripped sha256 05dd5f6a...), and an attended bench ladder
   (TEST-LADDER.md) with explicit PASS gates for 13/13 coverage, decoded BSS,
   bounded time, golden page and no firmware exception.

**Status: host-only, compile-clean, host-selftested; NOT bench-fired by this
lane.**  Nothing here is a hardware claim: the 13/13 PASS is the ladder's job.

| file | what |
| --- | --- |
| REPORT.md | measured baseline, request bytes + firmware read offsets, ranked hypotheses, tuning implementation, verification, open items |
| TEST-LADDER.md | attended ladder rungs R0-R8, PASS gates, decision table, stop rules |
| BUILD-NOTES.md | exact build/artifact numbers, build line, module parameters |
| recipes/REQUEST-BYTES.md | the 520-B frame hexdump + every field, with the firmware read site |
| recipes/BAND-CONFIG-G0ID10.md | H1 follow-up evidence: the missing scan-context config is g0 id10 (handler CA 0x5FF64) - field map + why no speculative builder is shipped |
| src/ | the four changed source files (diff base = the combined AP+DP tree) |
| patches/0001-scan-tuning.patch | integration diff over the bench-validated combined tree |
| tools/ | build.sh (host tests + module build), scan_tuning_selftest.c, inherited selftests, request_dump.c, fw_scan_timing_re.py (static RE evidence) |

Quick start (host, no device, no network):

    ./tools/build.sh

Safety: no device access, no vendor firmware/calibration byte is copied or
committed, `allow_frames` stays fail-closed by default, and the wire writes are
limited to the already-validated g0 id0xC scan request family (the request is
split, never extended).
