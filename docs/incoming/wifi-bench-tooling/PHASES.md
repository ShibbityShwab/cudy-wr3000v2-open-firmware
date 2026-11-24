# PHASES.md - bring-up phase matrix + status report (task-114)

**Lane:** wifi-bench-tooling (shared task **task-114**, extends task-97/108).
**Status:** HOST-ONLY, device-free tested. No device was contacted while writing
or testing this: every dry-run below deploys nothing and contacts nothing; the
attended runs stay the Lead's.

This document adds the **wave-2 bench automation**: a phase-matrix runner that
executes one selected bring-up phase through bench_session.py with a bundled,
data-driven sequence, and a status-report generator that renders the bring-up
state table:

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
| `C2` | calibration | protocol | `bringup-c2-calibration.json` | 28 / 54 / 8 | ctx_create type 1 -> ctx+0x190 allocated (signatures 0x64/3) -> txq_sync72 clean -> **six** cfg_cal 3502..3507, each consumed and each answered by a 298 B g1 id3 frame (relative catalogue delta + last_total=0x12A), counters frames_sent=8 ok=8 cfg_cal_sent=6 timeout=0 refused=0, page/state/mask still golden | calibration-token driver (**407,996 B sha256 2d45e545...**, attrs h2d_cfg_cal etc.), PD4 firmware cfe24bfb..., budget 8 |
| `D1` | vap | cfg80211 | `bringup-d1-vap.json` | 11 / 24 / 0 | cfg80211_status wiphy=alloc/registered=1 -> iw phy phy0 info -> iw phy phy0 interface add wlan0 type managed -> real netdev (sysfs type=1, address) -> VAP counters vap0: wlan0/created=1 -> iw dev wlan0 del -> vaps=0/removed=1, vap0 line absent, iw dev no longer lists wlan0 | cfg80211-enabled hi5622_wifi module (505,076 B d2b3cf79... or the newer scan-ap build 622,544 B 77987ed0...), cfg80211.ko 2,328,548 B 949393f9..., iw 121,952 B, libnl-tiny |
| `D2` | scan | cfg80211 | `bringup-d2-scan.json` | 11 / 15 / 0 | VAP up -> ONE iw dev wlan0 scan rc=0 -> mask 0 + staging 0 (no firmware exception) -> h2d_status timeout=0/refused=0; the D2H scan events are **watch-only** | the provider registry must carry the wifi-vap-scan g0 id0xC builder (520 B, SR node word1 0x02086d2b - ../wifi-vap-scan/recipes/frames.md); today the scan op is the fail-closed -EOPNOTSUPP stub (rc=95) |
| `E1` | dataplane | protocol | `bringup-e1-dataplane.json` | 11 / 22 / 3 | ready id2 payload 0x01060850 (MSDU-info ring-pointer table) -> clean ctx_create+txq_sync72 -> ONE read-only MAC slot-5 reachability probe (CA 0x40040000 via the region-3 translation, BAR0 0x3F8000; capture-only) -> ONE key-0x0320 TXQ-table address sync (h2d_txq_addr_sync, consumed, mask 0/staging 0, refused=0) | contracted dp-bringup token h2d_txq_addr_sync (the sequence JSON is the single edit point if the lane names it differently); MAC ring **register** programming (B2b) is the driver's job, not automation devmem |

The E1 MAC probe note: the datapath BENCH-LADDER text says "BAR0 0x402A000"
while the region-3 rule (BAR0 = 0x3B8000 + CA - 0x40000000) gives 0x3F8000 for
CA 0x40040000. The probe is capture-only and the session stops on an empty read
(no host write is attempted after a failed probe), so an attended B2a run
resolves the discrepancy safely; the result is recorded in the evidence.

---

## 2. Commands

Host-only dry-run of every phase (no device contact):

    cd opensource/docs/incoming/wifi-bench-tooling
    python3 tools/phase_matrix.py --list-phases
    python3 tools/phase_matrix.py --phase all --dry-run \
        --module ../wifi-h2d-send/src/hi5622_wifi.ko \
        --module-for D1=../wifi-cfg80211/src/hi5622_wifi.ko \
        --module-for D2=../wifi-cfg80211/src/hi5622_wifi.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /tmp/blocks/wifi_block.bin \
        --cfg80211-ko /path/cfg80211.ko \
        --iw-bin /path/iw --libnl-tiny /path/libnl-tiny.so

The matrix + per-phase evidence land in
build/evidence-bench-gates/wifi-bench-phase-matrix/<UTC-stamp>/
(gitignored; /tmp is never silently used). Then:

    python3 tools/bringup_report.py --dir build/evidence-bench-gates/wifi-bench-phase-matrix
    # writes/refreshes BRINGUP-STATUS.md next to the matrix; --json for machine output

Real attended run, one phase at a time (Lead only; recovery door armed,
read-only UART capture running):

    python3 tools/phase_matrix.py --phase C2 --yes-really --attended \
        --module /root-on-host/hi5622_wifi.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /tmp/blocks/wifi_block.bin --ssh-host 192.168.1.1

The underlying one-command session is unchanged:
`python3 tools/bench_session.py --mode {protocol|cfg80211} --dry-run ...`.

---

## 3. The cfg80211 mode (new image environment)

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

## 4. Sequence-schema additions (task-114)

See PROTOCOL-SEQUENCE.md for the full schema; task-114 adds:

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

---

## 5. Status semantics and the bring-up table

| status | meaning |
| --- | --- |
| PLAN-OK / READY | dry-run plan validated end to end (schema, pins, attrs in the real .ko, static script audit, sh -n); nothing deployed, no device contacted |
| PASS | real attended session, phase protocol verdict PASS |
| BLOCKED-TOKEN | target module lacks the driver token the phase needs (host-only .ko string probe) - an owning-lane deliverable gap, not a bench failure |
| BLOCKED-PREREQ | a local prerequisite artifact (cfg80211.ko / iw / libnl-tiny / budget) is missing |
| BLOCKED-DRIVER | real run stopped at a documented pending driver feature (e.g. cfg80211 scan stub rc=95) |
| STOP / FAIL / ERROR | real session stopped/failed, or the tool itself errored |
| NOT-RUN | phase not part of the selected matrix |

The report generator maps boot/ready to the boot phase record,
calibration->C2, vap->D1, scan->D2, dataplane->E1, and adds a "next action" for
every non-green row (e.g. wait for the g0 id0xC provider builder, then re-run
D2; wait for h2d_txq_addr_sync, then run E1).

---

## 6. Dry-run evidence (host-only, no device contacted)

Recorded 2026-11-25 from this lane (paths under
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T070509Z/,
gitignored; digests are the real local artifacts):

| phase | status | detail |
| --- | --- | --- |
| boot | **PLAN-OK** | 5 steps / 18 checks / 0 writes; page/state/mask/staging + ready-pair gates validated |
| C2 | **PLAN-OK** | 28 steps / 54 checks / 8 device writes; six 298 B response gates; budget=8 enforced |
| D1 | **PLAN-OK** | 11 steps / 24 checks / 0 writes; cfg80211.ko 2,328,548 B, iw 121,952 B, libnl-tiny validated, cfg80211-first load order + wiphy gate rendered, sh -n PASS |
| D2 | **PLAN-OK** | 11 steps / 15 checks; scan request gate + exception gates; D2H events watch-only |
| E1 | **BLOCKED-TOKEN** | the current calibration-token module carries no h2d_txq_addr_sync C string: dp-bringup deliverable pending; the E1 sequence JSON is the single edit point |

Bring-up status rendered from that matrix:

    boot READY | ready READY | calibration READY | vap READY | scan READY | dataplane BLOCKED (wait for dp-bringup token)

Selftest: **398/398** device-free checks (was 312), including the shell-step
refusal matrix, the shell/absent/present evaluation fixtures, the synthetic
C2 six-response evaluation (+ wrong-length and missing-response negatives), the
synthetic D1 wiphy+VAP add/del evaluation, the cfg80211 mode plan/deploy/script
(sh -n), the capability-probe refusals, phase-matrix end-to-end dry-runs
(PLAN-OK + BLOCKED-TOKEN) and the bring-up report mapping.

---

## 7. Not proven here

- No device was contacted and no frame was fired by this lane: every real
  phase run needs a human at the bench, the recovery door armed and a read-only
  UART capture, exactly like the other write modes.
- D2 is BLOCKED-DRIVER today: the cfg80211 scan op is the scan-ap lane's
  fail-closed -EOPNOTSUPP stub until the wifi-vap-scan g0 id0xC builder is
  registered in the provider registry.  Even then the firmware scan handler
  (CA 0x6494C) hardcodes VAP lookup id 0 while the M1 VAP is host-only, so the
  expected firmware outcome is a graceful no-VAP refusal with NO scan events -
  the D2 gate is therefore "request accepted + no exception", with D2H events
  captured watch-only.
- E1 is BLOCKED-TOKEN until the dp-bringup lane lands the TXQ-table address
  sync token; the contracted name h2d_txq_addr_sync is documented here and in
  the lane report so a rename is a one-line JSON change.
- The statuses are only as good as the pinned artifacts passed on the command
  line: the matrix records the .ko/.fw/sequence sha256 it actually validated.
