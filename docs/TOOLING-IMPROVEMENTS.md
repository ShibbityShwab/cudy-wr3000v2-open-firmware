# TOOLING IMPROVEMENTS: make the device loop faster and more reliable

Ranked, actionable list for the experiment harness. Every item names the exact files and the verification
it needs, and every quoted target was re-checked against the tree when this document was written
(2026-10-05). No device action is taken here. Ranked by impact/effort, 5 = best.

The harness this covers: `tools/exp.sh` (the cycle engine, `exp_run()` at `tools/exp.sh:536`), `tools/batch.sh`
(`batch_main()` at `tools/batch.sh:138`), `tools/patch_fw_scratch.py` (the variant builder), the per-lane
scripts under `build/tmp/wifidrv1-art/` (`run-*.sh`, `pack-*-evidence.sh`, `poll-*.sh`, `*-preflight.sh`,
`final-verify.sh`) and the two ssh wrappers `.sshwrap/rsh.sh` / `.sshwrap/rscp.sh`. Cycle baseline is about
150 to 160 s wall for one boot.

## Where the frictions come from

Three concrete wounds, not hypotheticals. First, `pack-gicking-evidence.sh` did not run, so gicking produced
no `interp.txt/acceptance.txt/cleanup.txt/pstore-check.txt` and left the staged `.omo-pat` on the device
(`mem-entries.md`, the gicking row). Second, the pack scripts are near-copies: 68 to 97 percent identical
after normalization, and the 8 `make_*` variants in `patch_fw_scratch.py` total 1115 lines with a +600-line
per-iteration delta that is copy-paste. Third, a measured fresh-auth ssh probe is 330 to 380 ms (4 samples),
paid on every probe. The ranked list below attacks those.

## Ranked recommendations

### 1. Recovery must delete every staged remote target (close the leftover-`.omo-pat` hole) - score 5.0

**What:** `exp_recover_body()` (`tools/exp.sh:132`) removes the module, loader and batch list but knows
nothing about `EXP_EXTRA_STAGE` targets, so `/lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat` survives unless a
pack script happens to run. Pass the remote half (`${line#*:}`) of each stage entry into the recover body and
`rm -f` it there; the device-side watchdog must do the same so a dead agent cannot strand a blob.

**Targets (all exist):** `tools/exp.sh:165-166` already holds `rm -f "/lib/modules/$KVER/$MOD"` and
`rm -f "/tmp/$MOD"`, so add `rm -f __STAGE_TARGETS__` beside them; template it in `exp_recover_body()`
(`tools/exp.sh:132`, the `sed -e` block just below) and export the list from `exp_stage()`
(`tools/exp.sh:258`, the `EXP_EXTRA_STAGE` loop at `tools/exp.sh:305-310`).

**Verification:** `bash tools/exp.sh --dry-run <ko>` must print the `rm -f` line; then one real cycle with
`EXP_EXTRA_STAGE` set, WITHOUT running any packer, and `gu-preflight.sh` must report `PATPRE=0` / `OMO_OFF=0`.

**Time saved:** removes the corrective cycle plus the manual verifier cleanup (about 10 to 15 min), and it is
a safety fix, not just speed.

### 2. Connection multiplexing in the ssh wrappers - score 3.0

**What:** every probe spawns a new password-auth ssh (measured 330 to 380 ms). Add
`-o ControlMaster=auto -o ControlPath=<.sshwrap/cm-%r@%h:%p> -o ControlPersist=60` (and a `mkdir -p` of the
ControlPath dir) so a cycle's roughly 45 to 70 ssh calls reuse one TCP/auth session.

**Targets (all exist):** `.sshwrap/rsh.sh` carries the `ssh -o StrictHostKeyChecking=no -o
PreferredAuthentications=password ...` invocation, and `.sshwrap/rscp.sh` the `scp -O -o
StrictHostKeyChecking=no ...` one; both are single exec lines with no multiplexing today.

**Verification:** time 4 probes before and after; expect roughly 340 ms down to under 80 ms; confirm the
socket file exists after the first call.

**Time saved:** about 15 to 25 s per cycle of pure auth latency across reboot-wait, wait-done, capture and
health polls; it also makes the ad-hoc pollers cheap (`poll-task12.sh` runs up to 37 probes).

### 3. Mandatory device preflight gate inside exp.sh - score 3.0

**What:** the freshness, no-leftover and vendor-up checks live in per-lane `*-preflight.sh` and were skipped
before a cycle. Add an `EXP_PREFLIGHT_CMD` (a remote one-liner) run in `exp_run()` BEFORE
`exp_arm_recovery`, failing closed with the exact leftover named.

**Targets (all exist):** the env-knob block at `tools/exp.sh:44-52` (add the new knob next to
`EXP_EXTRA_STAGE` and `EXP_CAPTURE_CMD`) and the `exp_run()` ordering (`tools/exp.sh:536`); reuse the body
already in `build/tmp/wifidrv1-art/gu-preflight.sh`.

**Verification:** `EXP_PREFLIGHT_CMD=false bash tools/exp.sh --dry-run <ko>` prints the gate and refuses
without touching the device; a dirty-router run stops before staging.

**Time saved:** prevents a wasted roughly 2.5 min boot (plus agent turns) on a dirty router.

### 4. Generalized `tools/pack-evidence.sh` plus a post-capture gate in exp.sh (flagship, highest absolute impact) - score 2.5

**What:** replace the near-identical `pack-*-evidence.sh` (68 to 97 percent dup) with one parameterized
packer: `pack-evidence.sh <lane>` assembles KNOBSET from `knobset-<lane>-head.txt` plus the frozen body,
copies the run log and hook, runs the lane's `*_verify.py`, writes the cleanup receipt, the pstore check and
the knob digest, and FAILS the run if any mandatory artifact (`KNOBSET.txt`, `interp.txt`, the hook,
`artifact-check`, `cleanup.txt`) is absent. Wire it via `EXP_PACK_CMD` at the end of `exp_capture()`
(`tools/exp.sh:367`) and re-invoke it for leftovers at `exp_recover()` (`tools/exp.sh:422`).

**Targets (all exist):** new `tools/pack-evidence.sh`; the capture printf at `tools/exp.sh:418`
(`printf '  evidence: %s\n' "$dir"`); a sample body to fold in, `build/tmp/wifidrv1-art/pack-gicunmask-evidence.sh`.

**Verification:** run one lane cycle, and its evidence dir must contain KNOBSET, the interp consumption, the
artifact-check and `cleanup.txt` (`PATPRE=0`) with no manual step; then delete an artifact and re-run, and the
packer must report `EVIDENCE INCOMPLETE` while the run prints `EXP RESULT: FAIL`.

**Time saved:** eliminates the missed-pack corrective cycle outright (about 2.5 min boot plus 10 to 20 min of
agent round-trips) and makes completeness machine-enforced instead of remembered.

### 5. `RUN-STATE.json` emitted by exp.sh so watchers watch a FILE - score 2.0

**What:** exp.sh writes a state machine
(`PREFLIGHT/ARMED/STAGED/BOOTED/RUNNING/CAPTURED/RECOVERED/HEALTHY/DONE|FAIL`) to `<EVID>/RUN-STATE.json` AND
a stable `build/register-dumps/exp/latest.json`, with `ts/phase/pid/evidence/verdict/health`. Watchers then
use a file monitor on `latest.json` instead of parking on a detached session and needing a cue.

**Targets (all exist):** `tools/exp.sh:536-565` (the `exp_run()` phase calls), add one `exp_state PHASE`
writer; `tools/exp.sh:418` for the CAPTURED transition.

**Verification:** during a `--dry-run`-instrumented stub, `cat build/tmp/exp-state.json` shows each phase in
order and ends `DONE health=PASS`; a file monitor fires once per transition.

**Time saved:** about 1 to 3 min per cycle of polling and turn round-trips; it removes the "workers park and
need cueing" friction.

### 6. One parameterized lane runner plus preflight/final-verify - score 1.5

**What:** collapse the 7 near-identical `run-*.sh` (only lane name, blob, hook and done-cmd differ) and the
duplicated preflight/final-verify into `tools/lane-run.sh <lane> <blob> <hook> <done-cmd>` that exports the
four `EXP_*` vars and backgrounds safely (as `run-gicunmask.sh` already does).

**Targets (all exist):** new `tools/lane-run.sh`; the 7 files it replaces
(`run-gicsend.sh`, `run-gicpost.sh`, `run-gicmask.sh`, `run-gicking.sh`, `run-gicunmask.sh`, `run-gicview.sh`,
`tools/wifidrv1-detached.sh`).

**Verification:** `bash tools/lane-run.sh gicunmask ... --dry-run` emits the identical env block to today's
`run-gicunmask.sh`; the real lane log matches `run-gicunmask.log` shape.

**Time saved:** about 5 to 10 min of per-variant authoring plus the elimination of run-script drift.

### 7. `tools/campaign.sh` - unattended sequential variants in one detached run - score 1.33

**What:** a manifest-driven runner that, for each lane, packs the blob (`patch_fw_scratch.py`), runs
`lane-run.sh` detached, waits on `latest.json` reaching `DONE|FAIL`, then records a per-lane row and moves on;
on FAIL it runs `final-verify.sh` and stops. One agent round-trip for N boots instead of N.

**Targets (all exist):** new `tools/campaign.sh`, consuming the item 4/5/6 outputs; mirrors the
`batch_main()` arg/list handling in `tools/batch.sh:138` as the pattern.

**Verification:** a 3-lane campaign with a deliberately failing lane stops after the FAIL, `final-verify.sh`
shows the router healthy, and the campaign summary lists each lane's verdict.

**Time saved:** about N times (one detached boot of 2.5 min plus agent turns); it removes the multi-lane
friction for N greater than 1.

### 8. Spec-driven variant registry plus a generic builder in patch_fw_scratch.py - score 1.0

**What:** the 8 `make_*` (1115 lines), 5 `verify_disasm_*` and 6 `select_cells_*` are near-copies (gicsend and
gicpost share 63 of 176 normalized lines). Define each variant as a spec dict (`base`, `sites[]` with
pad-builder plus cells plus manifest note, `pins[]`) and drive one generic `build(spec)` / `verify(spec)`; the
+600-line per-iteration delta becomes a roughly 30 to 80 line spec entry and the variant list becomes data,
not code.

**Targets (all exist):** the `choices=(...)` list and the dispatch chain in `tools/patch_fw_scratch.py`
(around the variant dispatch near line 3786) plus the pinned md5s at `tools/patch_fw_scratch.py:120-130`;
refactor `make_gicsend` and `make_gicunmask` into the table.

**Verification:** `patch_fw_scratch.py --selftest` must still regenerate all frozen variant md5s byte-identical
before and after the refactor (the pins at `tools/patch_fw_scratch.py:120-130`, including
`TRIGRING_MD5 = 0769eed122c0506d3afca6b5a8bd8ae9`).

**Time saved:** about 10 to 20 min per iteration of boilerplate authoring; it removes the copy-paste drift
class the pins currently catch.

### 9. Second-router note plus a per-router lock for parallel lanes - score 1.0

**What:** device access is serial (one router). Document the parallelization contract and make it
enforceable: an `EXP_HOST` override in exp.sh (currently hard-coded at `tools/exp.sh:36`,
`EXP_HOST="root@192.168.10.1"`) plus an `flock` on `build/tmp/exp-<host>.lock` so two lanes can never drive
one router concurrently. Then two lanes on two routers may run at once; a second router needs its own
`.omo-secrets` credential and a `192.168.10.1`-equivalent address.

**Targets (all exist):** `tools/exp.sh:36` (host) and `tools/exp.sh:536` (`exp_run`, acquire/release the lock);
`.sshwrap/rsh.sh` interpolates a fixed host, so it needs an `EXP_HOST`-aware form too.

**Verification:** start two lanes on one router, and the second exits with `lock held by <pid>` without
touching the device; on two routers both run and both end healthy.

**Time saved:** up to 2x wall-clock on a campaign once a second router exists.

## Top 3 quick wins

All three are single-file, under 15 line changes with no device-cycle risk:

1. **#1 Recovery deletes every staged `.omo-pat` target.** Closes the exact hole that stranded gicking's
   blob; safety plus one fewer corrective cycle. (`tools/exp.sh` recover body plus `exp_stage`.)
2. **#2 ssh ControlMaster multiplexing.** Roughly 340 ms down to under 80 ms per probe, about 15 to 25 s per
   cycle, touching only `.sshwrap/rsh.sh` and `.sshwrap/rscp.sh`.
3. **#3 Mandatory `EXP_PREFLIGHT_CMD` gate in exp.sh.** Fail closed on a dirty router before arming, reusing
   the already-written `gu-preflight.sh` body.

**Flagship (highest absolute impact, effort 2):** #4 `tools/pack-evidence.sh` plus the post-capture gate. It
makes evidence completeness (KNOBSET, interp, hook, cleanup) machine-enforced and deletes 5 duplicated pack
scripts.

**Flagship (highest absolute impact, effort 4):** #8 spec-driven variant registry. The only item that attacks
the +600-lines-per-iteration growth of `patch_fw_scratch.py` directly.

## Constraints that gate every item

One router, serial access; device cycles only serial/detached through `tools/exp.sh`; never write CA
`0x400392f0`; never read the RC misc window `0x10161000`; never read the GICC IAR CA `0x4016010c`. None of the
nine items touches the device on its own, and each carries a `--dry-run` or read-only verification.
