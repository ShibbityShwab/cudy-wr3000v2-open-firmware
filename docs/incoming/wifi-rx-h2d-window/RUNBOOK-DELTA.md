# RUNBOOK-DELTA - the stock H2D trace RETRY (task-194)

This is the **delta** to `docs/incoming/wifi-rx-h2d-trace/RUNBOOK.md` (task-176's plan). That
runbook's T0-T3, T5-T7 and all hard rules stand unchanged. Only what follows changes; the retry
reruns the *same* attended procedure with the v2 instrument. Owner of the fire: the Lead; this
lane is host-only.

**Read first:** REPORT.md §2 (why v1 failed), §3 (EP1 window), §4 (timing). Do not rerun v1:
it fails identically at any `-w`, by construction.

## D0. Build the v2 instrument (host, ~2 min)

```sh
cd /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
D=opensource/docs/incoming/wifi-rx-h2d-window
$D/tools/build-sampler-mmap.sh
# -> build/wifi-rx-h2d-window/omo_h2dsampler-mmap  (static ARM/musl; file + sha256 printed)
file build/wifi-rx-h2d-window/omo_h2dsampler-mmap
sha256sum build/wifi-rx-h2d-window/omo_h2dsampler-mmap
```
Expected: `build/wifi-rx-h2d-window/omo_h2dsampler-mmap`, 54,364 B, static ARM/musl,
`sha256 cb03ee1b85feb7fdd0a81b3b7a4d97461a8ad2eb1a9459134426ddbbbf352805` (the byte-exact
artifact this lane tested host-side). Run `$D/tools/run-host-tests.sh` first; expected output is
`evidence/HOST-TESTS.txt` (12/12 + selftest 17/17). Record the sha256 in the session evidence.

## D1. Payload substitution (during T0/T3; same paths as v1)

* Put **`omo_h2dsampler-mmap`** at `/usr/bin/omo_h2dsampler` in the payload tree instead of
  the v1 binary. The aid script (`omo-h2dtrace`) needs one edit for the new arguments:
  replace its sampler line with

  ```sh
  /usr/bin/omo_h2dsampler -o "$LOG" -t 240 -r 20 -w 0x583f2000 -W 180 >>"$ERR" 2>&1 ) &
  ```

  (v1 line: `-o "$LOG" -t 180 -r 20`; the v2 defaults are already `-w 0x583f2000`, so the
  explicit `-w` is for the record. `-W 180` is insurance only - the decode says the ring is
  live ~4 s after the S08.)
* Everything else in T0/T3 (container crafting, tfTP install, permissions) is unchanged.

## D2. NEW T2.5 - read-only window/access gate (MANDATORY, before the capture boot)

On the stock root shell **after** the vendor wifi stack is up (`grep hi5622v100_plat
/proc/modules` matches; the AP is up), with the v2 binary already installed:

```sh
/usr/bin/omo_h2dsampler -p -w 0x583f2000
```

Expected (the retry's first evidence lines):

* `window=0x583f2000 access=mmap`
* SR ch0: `base=0x8xxxxxxx` (non-zero, in `0x80000000..0x87ffffff`), `depth=0x1f`,
  producer/consumer counters present, `ring=YES`;
* `pread_control=FAIL errno=14 (Bad address)` - i.e. **EFAULT**: this is the v1 defect measured on the device
  (REPORT §2) - **expected**, not a fault;
* optional alias check `/usr/bin/omo_h2dsampler -p -w 0x403f2000`: same SR0 base.

**STOP rules (do not boot the capture):**
* mmap/open error, or `base=0x00000000` / `base=0xffffffff`, or depth not `0x1f`;
* `ring=NO` at `-w 0x583f2000` **and** at `-w 0x403f2000`;
* any oops/abort in the UART while probing (record; power-cycle; report - a STOP is a finding).
If only `-w 0x403f2000` shows the ring, use `-w 0x403f2000` for the capture and note it (the
two windows alias; this would mean EP1's region-3 was not programmed in that boot).

## D3. T4 - the capture boot (one attended boot)

Unchanged except the sampler arguments (D1). PASS evidence in `/overlay/h2d-trace.log`:

* line 1: `# omo_h2dsampler v2 ts0=… window=0x583f2000 module=hi5622v100_plat … access=mmap`;
* `E ts_ms=… msg=ring-seen dt_wait_ms=N` with **N well under 60000** (decode predicts
  ~4 s; if N is large, still fine up to -W);
* `R ts_ms=… ch=0 base=0x8xxxxxxx depth=0x1f prod=… cons=…` lines (>=1/s);
* `M ts_ms=… ch=0 … hex=…` lines (the H2D stream);
* heartbeats carry `misreads=0` for the window (erroneous DRAM node reads are counted too).

**If `ring-not-seen` still appears:** the v2 log now carries `E msg=win-probe base=… depth=…
prod=…` with the raw last register read, and `misreads=` in the heartbeats. STOP, collect the
log, and report - that combination distinguishes "not programmed", "not readable" (mmap error)
and "device not ready" without another guess.

## D4. T5/T6/T7 - unchanged

* T5 log transfer/sha256: unchanged (`h2d-trace.log`, `h2d-trace.err`).
* T6 decode: unchanged - the v2 log format is v1-compatible:
  `.provision-venv/bin/python opensource/docs/incoming/wifi-rx-h2d-trace/tools/decode_h2d.py` …
* T7 cleanup: remove `/usr/bin/omo_h2dsampler` (the v2 binary lives at the same path), the aid,
  the S08 symlink and the logs; switch back to pair B exactly as before.

## D5. Time budget delta

| phase | estimate |
| --- | --- |
| D0 build v2 + host tests | 5 min |
| D2 window gate | 5 min |
| everything else | as task-176's table |

One capture boot per sitting; a failed gate is a finding, not a retry. All hard rules carried
from BENCH-FACTS/HAZARDS apply unchanged (UART read-only; recovery card READY; no writes to
missing hardware; never `rmmod` the vendor modules; stock pair A is the control image).