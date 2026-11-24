# PHASES.md - bring-up phase matrix + status report (task-114; D2 ladder task-124)

**Lane:** wifi-bench-tooling (shared task **task-114**, extends task-97/108;
task **task-124** extends the D2 row on 2026-11-26).
**Status:** HOST-ONLY, device-free tested. No device was contacted while writing
or testing this: every dry-run below deploys nothing and contacts nothing; the
attended runs (including the D2 TRUE-PASS fire) stay the Lead's.

This document describes the **wave-2/3 bench automation**: a phase-matrix
runner that executes one selected bring-up phase through bench_session.py with
a bundled, data-driven sequence, and a status-report generator that renders the
bring-up state table:

    boot | ready | calibration (C2) | vap (D1) | scan (D2) | dataplane (E1)

The cfg80211 image environment (cfg80211.ko + iw + libnl-tiny) is a first-class
mode: `bench_session.py --mode cfg80211` preflights the module order, deploys
the missing userspace to /tmp, and gates the wiphy registration that the driver
performs after the ready handshake.

---

## 1. The phases

| phase | row | mode | sequence (bundled) | steps/checks/writes | PASS gate | external dependency |
| --- | --- | --- | --- | --- | --- | --- |
| `boot` | boot + ready | protocol | `bringup-boot-ready.json` | 5 / 18 / 0 | page 0x86807FEF/7/2/0xABCD, state 1, mask 0, staging 0; ready id1+id2, id2 payload 0x01060850, phase READY_TXQ_SYNC | none (read-only) |
| `C2` | calibration | protocol | `bringup-c2-calibration.json` | 28 / 55 / 8 | ctx_create type 1 -> ctx4+0x190 allocated + the measured ctx4 body PACK (0x7D4458=4, 0x7D4468=0x00010001, 0x7D488C=0x0100478C) -> txq_sync72 clean -> **six** cfg_cal 3502..3507, each consumed and each answered by a 298 B g1 id3 frame (relative catalogue delta + last_total=0x12A), counters frames_sent=8 ok=8 cfg_cal_sent=6 timeout=0 refused=0, page/state/mask still golden | calibration-token driver (407,996 B sha256 2d45e545..., attrs h2d_cfg_cal etc.), PD4 firmware cfe24bfb..., budget 8 |
| `D1` | vap | cfg80211 | `bringup-d1-vap.json` | 11 / 24 / 0 | cfg80211_status wiphy=alloc/registered=1 -> iw phy phy0 info -> iw phy phy0 interface add wlan0 type managed -> real netdev (sysfs type=1, address) -> VAP counters vap0: wlan0/created=1 -> iw dev wlan0 del -> vaps=0/removed=1, vap0 line absent, iw dev no longer lists wlan0 | cfg80211-enabled hi5622_wifi module (505,076 B d2b3cf79... or the newer scan-ap build), cfg80211.ko 2,328,548 B 949393f9..., iw 121,952 B, libnl-tiny |
| `D2` | scan | cfg80211 | `bringup-d2-scan.json` | **22 / 48 / 2** | **TRUE-PASS ladder**: ready -> ctx_create t1 (g0 id1 key 0x64) -> txq_sync72 -> `iw phy phy0 interface add wlan0 type managed` (provider user_add g0 id7 + notify g0 id8) -> link up -> ONE `iw dev wlan0 scan` (provider vap_add g0 id1 48 B + vap_start g0 id1 40 B + scan_req g0 id0xC 520 B). PASS = iw rc=0 **and** dmesg `cfg80211: scan channel result slot=.. ch=.. (.. MHz)` present **and** scan results>0 (cfg80211_status `results=` present, `results=0` absent) **and** mask/staging 0 **and** golden page/state **and** provider frame_post refused=0/timeout=0/bad=0. The D2H g1 15/16/17 (and g2 2) catalogue is **captured as evidence**, not gated (the PASS decision is the scan result set) | **the wifi-vap-start PROVIDER module** (wifi-scan-ap src + `../wifi-vap-start/provider/vapstart-provider.patch`, mode `vap_start_mode=1`, `allow_frames=12`, 738,112 B sha256 e09dc697...; build with `../wifi-vap-start/tools/kernel-check.sh`). The plain cfg80211/scan-ap module has no `vap_start_*` params: D2 is refused at plan time (module-param-unknown -> **BLOCKED-PREREQ**), never silently mis-run. PD4 firmware cfe24bfb..., cfg80211.ko/iw/libnl-tiny as D1 |
| `E1` | dataplane | protocol | `bringup-e1-dataplane.json` | 13 / 42 / 3 | ready id2 payload 0x01060850 (MSDU-info ring-pointer table) -> clean ctx_create -> ONE read-only MAC slot-5 reachability read via the RO dp_probe attribute -> ONE dp_setup (TXQ table 0x2524 + 8x64x8 TX MSDU rings + tx_ba_info + RX 256/512 + 4095 completion ring; 14 MAC band-0 registers written+verified, 8 device-RAM table entries) -> dp_rings snapshot -> ONE key-0x0320 h2d_txq_addr_sync (txq_addr_sent=1, ok=2, timeout=0) -> post pack mask/staging 0, state 1, page golden, out0/out2 0 | the wifi-datapath-bringup driver (module 462,952 B sha256 8fc693ad..., attrs dp_probe/dp_setup/dp_txq_sync/dp_status/dp_rings + h2d_txq_addr_sync alias), PD4 firmware cfe24bfb..., budget 2; MAC ring register programming is the driver's action, never automation devmem |

**D2 provider prerequisites, in order (why the ladder is not just "iw scan"):**
the provider patch enables `vap_start_first=1`, which posts the h2d VAP-create
(g0 id1 key 0x64) + VAP-start (g0 id1 key 0x65) family inside the scan op; that
code path requires the hCC core to be ACTIVE, i.e. ctx_create type 1 + the
72-byte TXQ config sync must already be consumed. The 2026-11-26 14:00
hardware-validated chain is exactly the D2 sequence: boot golden -> C2 ctx+txq
consumed -> iface add (user_add 180 B + notify 180 B, 1/12 + 2/12) -> scan
(vap_add 48 B 3/12 -> vap_start 40 B 4/12 -> scan_req 520 B 5/12) -> firmware
`scan channel result` ch1-6 + `scan DONE (finish-abort, results=6 bss=6
complete=0 channel=6 ...)` + rd_catalog g1 id17 count=6 / g1 id15 count=6.
The provider budget is `allow_frames=12` (smoke test needs 5); the D2
sequence pins `budget_min=8` and `h2d_send_budget=8` (2 H2D-h2d writes +
5 provider frames fit with headroom).

**E1 MAC probe note:** the datapath BENCH-LADDER text said "BAR0 0x402A000"
while the region-3 rule gives 0x3F8000 for CA 0x40040000. The dp-bringup driver
performs this as a driver-mediated read-only word read exposed through the RO
dp_probe attribute (captured in the evidence, value not gated), so no raw BAR0
probe from userspace is needed.

---

## 2. Commands

Host-only dry-run of every phase (no device contact):

    cd opensource/docs/incoming/wifi-bench-tooling
    python3 tools/phase_matrix.py --list-phases
    python3 tools/phase_matrix.py --phase all --dry-run \
        --module ../wifi-h2d-send/src/hi5622_wifi.ko \
        --module-for D1=../wifi-cfg80211/src/hi5622_wifi.ko \
        --module-for D2=/tmp/wifi-vap-start-kernel/hi5622_wifi.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /tmp/blocks/wifi_block.bin \
        --cfg80211-ko /path/cfg80211.ko \
        --iw-bin /path/iw --libnl-tiny /path/libnl-tiny.so

D2-only dry-run (the row this document's task-124 evidence was recorded with;
the sequence load_params carry the provider params, the CLI only overrides):

    python3 tools/phase_matrix.py --phase D2 --dry-run \
        --module-for D2=/tmp/wifi-vap-start-kernel/hi5622_wifi.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /tmp/blocks/wifi_block.bin \
        --cfg80211-ko /home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon/lib/modules/6.18.60/cfg80211.ko \
        --iw-bin /tmp/rootfs2524b/usr/sbin/iw \
        --libnl-tiny /tmp/rootfs2524b/usr/lib/libnl-tiny.so \
        --module-param-for D2=frame_gap_ms=100 \
        --report-dir build/evidence-bench-gates/wifi-bench-phase-matrix

The same D2 command on the plain `../wifi-scan-ap/src/hi5622_wifi.ko` (no
`vap_start_*` params) is the host-only negative control: **BLOCKED-PREREQ**
(module-param-unknown: allow_frames) - use the provider module for a real D2.

New per-phase flags (task-124):

- `--module-for PHASE=PATH` - per-phase module (D2 needs the provider module).
- `--module-param-for PHASE=KEY=VALUE` (repeatable) - per-phase insmod
  parameter; applies only to that phase, after the sequence's own
  `load_params` (so it overrides them). Global `--module-param KEY=VALUE`
  still applies to every selected phase; the sugar `--module-param
  PHASE:KEY=VALUE` targets one phase.
- `--sequence-for PHASE=PATH` (repeatable) - per-phase sequence document, for
  a variant matrix row; the bundled sequence stays the default.

The matrix + per-phase evidence land in
build/evidence-bench-gates/wifi-bench-phase-matrix/<UTC-stamp>/
(gitignored; /tmp is never silently used). Then:

    python3 tools/bringup_report.py --dir build/evidence-bench-gates/wifi-bench-phase-matrix
    # writes/refreshes BRINGUP-STATUS.md next to the matrix; --json for machine output

Real attended D2 run (Lead only; recovery door armed, read-only UART capture
running; the mode-1 provider module is deployed from the host):

    python3 tools/phase_matrix.py --phase D2 --yes-really --attended \
        --module-for D2=/root-on-host/hi5622_wifi-provider.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /tmp/blocks/wifi_block.bin \
        --cfg80211-ko /root-on-host/cfg80211.ko --iw-bin /root-on-host/iw \
        --libnl-tiny /root-on-host/libnl-tiny.so --ssh-host 192.168.1.1

For several real phases in one sitting add **--reboot-between** (reboots after
every phase except the last) or **--reboot-after** (reboots after every phase):
a loaded hi5622_wifi makes the next phase stop at the module-already-loaded gate
/ insmod rc=255, so back-to-back phases need a fresh boot between them.  The
**matrix** owns that reboot: it issues /sbin/reboot -f in the background and
waits with a HARD deadline (--reboot-deadline, default 300 s; --reboot-poll,
default 4 s), logging '[reboot]' progress; if the box does not come back the
remaining phases are marked NOT-RUN (never run on a stale box).  Each real
phase child also has a hard --phase-timeout (default 900 s) and is killed as a
whole process group on timeout, so no bench_session/ssh orphan survives the
run.

The underlying one-command session is unchanged:
`python3 tools/bench_session.py --mode {protocol|cfg80211} --dry-run ...`.

---

## 3. The cfg80211 mode (image environment)

`bench_session.py --mode cfg80211` is protocol mode plus the userspace the
2026-11-25 cfg80211 image does not carry in its rootfs:

| artifact | flag | validation | deploy target | use |
| --- | --- | --- | --- | --- |
| cfg80211.ko | `--cfg80211-ko` | ELF32 ARM ET_REL, GPL, vermagic 6.18.60 SMP ARMv7 (support-module policy) | /root/cfg80211.ko | insmod BEFORE the instrument (module-order gate) |
| iw | `--iw-bin` | static ARM ET_EXEC (ELF32 LE) | /tmp/hi5622-bench-iw | all nl80211 actions in the sequence |
| libnl-tiny | `--libnl-tiny` | ARM ET_DYN shared object | /tmp/<basename> | LD_LIBRARY_PATH=/tmp for iw |

In the generated device script (in this order):

1. sha256sum read-back of every support file before anything is loaded (deploy
   already sha-verifies; this is the on-box re-check).
2. **module order**: cfg80211 must be present (`/sys/module/cfg80211`, built-in
   or insmoded) BEFORE the instrument is loaded; the script stops on an insmod
   failure or a missing /proc/modules entry.
3. IW selection: deployed /tmp/hi5622-bench-iw if executable, else
   `command -v iw`, else stop tool-missing.
4. after the strict ready pair: wait up to --cfg-ready (default 20 s) for
   /sys/class/ieee80211/phy* (the driver registers the wiphy from a workqueue
   after the ready handshake), gate cfg80211_status registered=1, then run
   iw phy as a read-only enumeration gate before the sequence starts.

If any support artifact is missing on a real run the plan is refused; in a
dry-run the deploy entry is recorded with an unverified placeholder and the
plan warns loudly, so the incompleteness is visible.

---

## 4. Sequence-schema additions (task-114 + task-124)

See PROTOCOL-SEQUENCE.md for the full schema; task-114 added the shell step
kind, profiles, require extensions, capability probe and expectation
extensions.  task-124 adds the documented-pending (BLOCKED-DRIVER) gates and
the per-phase configuration:

- **shell step kind** - ONE allowlisted userspace command built from
  SHELL_TEMPLATES (no free text ever reaches the device script):
  iw-dev-list, iw-phy-list, iw-phy-info, iw-iface-add, iw-iface-del, iw-scan,
  ip-link-up, ip-link-down, sysfs-read (only /sys/class/{ieee80211,net}/...),
  dmesg.  Fields: cmd, typed args, expect_rc (0..255), patterns (must appear),
  absent (must NOT appear).  iw commands are only valid in the
  hi5622-wifi-cfg80211 profile.
- **profiles**: hi5622-wifi-h2d (protocol mode) and hi5622-wifi-cfg80211
  (cfg80211 mode); the runner refuses a sequence whose profile does not match
  the mode.
- **require additions**: module_sha256, cfg80211 (bool), attrs (sysfs
  attributes the phase needs - host-probed against the target .ko), tools
  (iw/ip/dmesg).
- **capability probe**: every attr a sequence touches plus require.attrs is a
  driver deliverable; for a real module (size >= 4 KiB) the runner scans the
  .ko for the attribute/token C strings and refuses at plan time with
  module-attr-missing if absent.  That is what makes a missing wave-3 token
  show up as **BLOCKED** in the matrix before any bench time.  (Tiny synthetic
  selftest fixtures skip the probe with a warning.)
- **expectation extensions**: present (a raw read must be non-empty -
  reachability probes), absent patterns on attr_expect, and relative catalogue
  deltas min_count_delta + exact last_total on d2h_catalog (the six 298 B
  calibration answers are gated that way).
- **documented-pending gates (BLOCKED-DRIVER)** - two forms, used by D2:
  - `attr_expect.blocked_patterns`: if the PASS gate (patterns/absent) fails
    but the captured attribute matches a documented-pending signature (D2:
    `results=0`), the step records BLOCKED, the session continues and the
    session result is BLOCKED:<step>; any other failure still stop-on-first.
  - `shell.blocked_patterns` **without** `blocked_rc`: signature-driven form
    for a command that returns its normal rc (e.g. dmesg rc=0) while the
    required result pattern is absent and the driver logged the documented
    pending signature (D2: `scan DONE (finish-abort, results=0 ...)`).
    `blocked_rc` + signatures (the legacy fail-closed stub form) is unchanged.
- **per-phase configuration**: phase_matrix `--module-for`,
  `--module-param-for`, `--sequence-for` (see section 2).  bench_session still
  takes one invocation per phase; the sequence `load_params` are the phase's
  own defaults and CLI `--module-param` overrides them.

---

## 5. Status semantics, and the D2 PASS vs BLOCKED-DRIVER decision table

| status | meaning |
| --- | --- |
| PLAN-OK / READY | dry-run plan validated end to end (schema, pins, attrs in the real .ko, static script audit, sh -n); nothing deployed, no device contacted |
| PASS | real attended session, phase protocol verdict PASS |
| BLOCKED-TOKEN | target module lacks the driver token the phase needs (host-only .ko string probe) - an owning-lane deliverable gap, not a bench failure |
| BLOCKED-PREREQ | a local prerequisite artifact is missing, or the plan needs a different module build (e.g. D2 on the plain module -> `module-param-unknown`), or a budget/window prerequisite is not met |
| BLOCKED-DRIVER | real run reached a documented pending driver/firmware feature: a blocked_rc shell step, or a blocked_patterns signature gate (D2 results=0). The session continues, its result is BLOCKED and the matrix maps it from the session/evidence artefacts, never from the exit code alone |
| STOP / FAIL / ERROR | real session stopped/failed, or the tool itself errored |
| NOT-RUN | phase not part of the selected matrix |

### 5.1 D2 decision table (task-124)

Every row is what the code evaluates; the two BLOCKED rows are the only
non-FAIL "the ladder ran but the firmware produced nothing" outcomes.

| observed | iw scan rc | dmesg `scan channel result` | scan results | mask/staging | page/state | outcome |
| --- | --- | --- | --- | --- | --- | --- |
| scan works | 0 | present (>=1 line) | >0 | 0 | golden | **PASS** |
| ladder ran, firmware produced nothing | 0 (iw prints `scan aborted!` and returns 0) | absent **and** driver `scan DONE (finish-abort, results=0 ...)` present | 0 | 0 | golden | **BLOCKED-DRIVER** (documented pending: VAP/scan-state wall or decode gap) |
| legacy fail-closed stub | 95/161 + documented signature (`Not supported` / `scan needs an H2D frame provider`) | - | - | - | - | **BLOCKED-DRIVER** (pending scan-provider capability) |
| gate mismatch (no signature) | 0 | absent, no results=0 signature | any | 0 | golden | **STOP/FAIL** (inspect evidence; do not rescan) |
| firmware exception during the ladder | any | - | any | nonzero | any | **FAIL** (never BLOCKED) |
| provider/tool refusal | other nonzero | - | - | - | - | **FAIL/STOP** (config or provider bug; inspect dmesg) |
| page drift / boot state lost | any | - | - | - | drift | **FAIL** |

Stop rules encoded in the sequence: one scan request per boot; no retry on the
same boot; a BLOCKED results=0 session still continues to the mask/staging/page
pack so the evidence shows whether the firmware stayed healthy; never raise
`allow_frames`/budget mid-session (it is an insmod param).

The report generator maps boot/ready to the boot phase record,
calibration->C2, vap->D1, scan->D2, dataplane->E1, and adds a "next action" for
every non-green row (e.g. "run phase D2 on the wifi-vap-start provider module",
then re-run when the row is green).

---

## 6. Dry-run evidence (host-only, no device contacted)

### 6.1 task-124: D2 TRUE-PASS ladder (recorded 2026-11-24 09:41 UTC)

Real local artefact digests (all read in place, nothing committed):

| item | value |
| --- | --- |
| sequence | `bringup-d2-scan.json` sha256 **7167dd997ce8f448805e0ff6826d227996bace775db46ffb452241df04e7a77b** |
| D2 row | **PLAN-OK** - 22 steps / 48 checks / 5 shell actions / **2 device writes** (ctx_create t1, txq_sync72) |
| D2 provider module | 738,112 B sha256 **e09dc697bd305c599c26712f60994c8e86cf127e00cfe329002e9979067dc4bf** (wifi-vap-start mode-1 provider build) |
| sequence load_params | allow_frames=12, frame_gap_ms=100, h2d_send_budget=8, vap_start_first=1, vap_start_mode=1, vap_start_module=0 |
| per-phase CLI param | `--module-param-for D2=frame_gap_ms=100` (recorded in the matrix phase config) |
| PD4 firmware | cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1, 928,920 B |
| wifi PHY block | 0b4ef9188fc1c9160b4b30cc20be0e704dd62412cbdb25dd51ea5ffb16145b32, 2,472 B |
| cfg80211.ko | 2,328,548 B sha256 949393f9... (6.18.60, GPL) |
| iw / libnl-tiny | iw 121,952 B 348dfdac... ; libnl-tiny 30,252 B 3c2ff8b9... |
| generated script | `sh -n` PASS; static write audit PASS; insmod carries the six provider params; cfg80211 loads before the instrument |
| matrix | `build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T094057Z/` (gitignored) |
| negative control | the same D2 on the plain scan-ap module (704,720 B sha256 20d38c5c...) is **BLOCKED-PREREQ** (module-param-unknown: allow_frames), recorded under `.../plain-module-reference/20261124T094058Z/` |

The generated D2 device script is the exact attended ladder: ctx_create t1 ->
post-create ctx4 PACK -> txq_sync72 -> post-txq pack -> iw iface add ->
link up -> vap-added -> scan -> scan-settle -> scan-results attr gate ->
scan-dmesg gate -> mask/staging pack -> page/state pack -> frame_post gate ->
h2d summary -> catalogue capture -> vap del.

### 6.2 task-114 baseline (2026-11-25, previously recorded)

| phase | status | detail |
| --- | --- | --- |
| boot | **PLAN-OK** | 5 steps / 18 checks / 0 writes; page/state/mask/staging + ready-pair gates validated |
| C2 | **PLAN-OK** | 28 steps / 55 checks / 8 device writes; six 298 B response gates; budget=8 enforced |
| D1 | **PLAN-OK** | 11 steps / 24 checks / 0 writes; cfg80211.ko 2,328,548 B, iw 121,952 B, libnl-tiny validated, cfg80211-first load order + wiphy gate rendered, sh -n PASS |
| D2 (old pre-task-124 row) | **PLAN-OK** | 11 steps / 15 checks; scan-request gate + no-exception gates; D2H events watch-only |
| E1 | **PLAN-OK** | 13 steps / 42 checks / 3 writes on the dp-bringup module (462,952 B sha256 8fc693ad...); with the calibration-token module E1 is BLOCKED-TOKEN (it has no dp_* C strings) |

Real attended history: the 2026-11-25 full matrix ran boot/C2/D1/E1 PASS and
D2 BLOCKED-DRIVER (old fail-closed scan stub); the 2026-11-26 14:00 mode-1
provider fire produced the first real firmware scan results (see section 7).

### 6.3 Full-matrix regression (same provider module on every phase)

`--phase all --dry-run` with the provider module recorded **boot PLAN-OK
(5/18/0), D1 PLAN-OK (11/24/0), D2 PLAN-OK (22/48/2)**, and C2/E1
**BLOCKED-TOKEN** (the provider module carries no `h2d_cfg_cal` / `dp_*` C
strings - the calibration-token and dp-bringup modules are their own lanes).
Matrix: `build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T094059Z/`
(gitignored) with the rendered `BRINGUP-STATUS.md`:
boot READY | ready READY | calibration BLOCKED | vap READY | scan READY |
dataplane BLOCKED.

### 6.4 Selftest

`./tools/run-selftest.sh` - **542/542** device-free checks (was 503),
including the new blocked-patterns gate tests (schema/refusals, device-side
script execution under `sh -u`, host evaluation PASS/BLOCKED/FAIL), the
synthetic D2 decision-table matrix (hardware-result PASS, results=0 BLOCKED,
firmware-exception FAIL, page-drift FAIL, unsigned gate-miss FAIL), the
per-phase module-param/-sequence tests and the provider-vs-plain-module D2
matrix dry-runs (PLAN-OK vs BLOCKED-PREREQ).

---

## 7. Not proven here (and the hardware hand-off)

- No device was contacted and no frame was fired by this lane in task-124:
  every real phase run needs a human at the bench, the recovery door armed and
  a read-only UART capture, exactly like the other write modes.  The D2 ladder
  is host-tested (schema, plan, generated script, synthetic evaluation) and its
  PASS observables are the hardware-validated ones, but the **fire** is the
  Lead's.
- The D2 TRUE-PASS state is from the 2026-11-26 14:00 attended mode-1 provider
  fire: dmesg `cfg80211: scan channel result slot=0 ch=1 (2412 MHz)` ...
  `slot=5 ch=6 (2437 MHz)`, `scan DONE (finish-abort, results=6 bss=6
  complete=0 channel=6 sta=0 unknown=0 dropped_busy=0 timeout=1)`, rd_catalog
  g1 id17 count=6 (124 B) + g1 id15 count=6 (509 B), page 0x86807FEF/7/2/ABCD,
  mask 0, staging 0.  The scan ends finish-abort at the provider's 5 s bound
  after 6 of 13 channels; that is SATISFYING the results>0 gate, not the
  full-channel-completion goal (a separate scan-completion lane task-123).
- Until the provider .ko is delivered/installed by the owning lane, a bench
  host must build it with `../wifi-vap-start/tools/kernel-check.sh`
  (`/tmp/wifi-vap-start-kernel/hi5622_wifi.ko`, sha e09dc697...).  The module
  is a local build artefact and is never committed.
- E1 is PLAN-OK against the wifi-datapath-bringup driver (module 462,952 B
  sha256 8fc693ad...); with any other module it is BLOCKED-TOKEN.  The dp_setup
  MAC register program is the driver's action, not automation devmem, and it
  has not been fired on the bench by this lane.
- The statuses are only as good as the pinned artifacts passed on the command
  line: the matrix records the .ko/.fw/sequence sha256 it actually validated.
