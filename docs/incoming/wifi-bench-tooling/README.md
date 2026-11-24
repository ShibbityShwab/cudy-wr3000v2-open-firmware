# wifi-bench-tooling - one-command bench sessions + patched-firmware builder/verifier

**Lane:** wifi-bench-tooling (shared tasks **task-97** + **task-108**),
host-only, 2026-11-24/25.  No device was contacted while writing or testing
this lane: every check in the selftest runs on synthetic data, and every
dry-run command below explicitly refuses to touch the router. The attended
session itself is the Lead's to run.

**task-108 extension:** a data-driven **protocol-sequence** mode
(+tools/protocol_sequence.py, +tools/sequences/) that runs the post-boot H2D
startup as an ordered, per-step-gated sequence with a JSON + markdown evidence
record - PD4 golden boot -> context-create type 1 -> PACK -> TXQ config sync 72
-> PACK -> D2H response delta.  See **PROTOCOL-SEQUENCE.md** for the schema,
exact commands and the failure triage table.

**task-114 extension (wave 2):** the **cfg80211 bench mode** + the **bring-up
ladder**: bundled sequences for phases **C2** (six-calibration boot),
**D1** (cfg80211 VAP add/del), **D2** (scan request) and **E1** (data-plane
setup + TXQ-table sync); an allowlisted `shell` step kind for iw/ip/sysfs
actions; a cfg80211 mode that deploys/loads cfg80211.ko + iw + libnl-tiny and
gates the wiphy registration; `tools/phase_matrix.py` (one command per phase,
evidence JSON + markdown) and `tools/bringup_report.py` (the
boot/ready/calibration/vap/scan/dataplane state table).  See **PHASES.md**.


**Read first:** STATUS.md sections 3b/3c, the newest mem-entries.md entries
(2026-11-24 GP8/GP9/GP10/PD), and the lane reports this tool consumes:
wifi-irq-unmask/REPORT.md (PD + PD2 recipe), wifi-hcc-service/BENCH-PROCEDURE.md,
wifi-d2h-direct/BENCH-PROCEDURE.md, wifi-transport-port/build/ (sysfs inventory).

## What this is

| tool | what it does |
| --- | --- |
| tools/bench_session.py | one command for an attended Wi-Fi bench session: modes stock-golden / our-golden / patched / h2d / **protocol**; host preflight, deploy+sha read-back, module load with the fw_path variant, prepare/upload/verify/seed, hcc_arm/hcc_loop, release-only, golden-page gate, strict ready pair, then either the one-shot announce or the data-driven protocol sequence, structured report + sha256-chained write audit |
| tools/protocol_sequence.py | the data-driven protocol-sequence engine: JSON schema validation (fail-closed), shell-gate + pointer-convention code generation, independent host-side re-evaluation of every gate, evidence JSON/markdown rendering, failure triage; task-114 adds the allowlisted `shell` step kind, profiles, `present`/`absent`/delta expectations |
| tools/sequences/ladder-c-ordered.json | the bundled hardware-validated ladder: golden boot -> ctx_create t1 -> PACK -> txq_sync72 -> PACK -> D2H delta (11 steps / 62 checks / 2 H2D writes) |
| tools/sequences/bringup-*.json | the task-114 bring-up ladder: boot-ready (read-only), C2 six-calibration (28 steps / 8 writes), D1 VAP add/del, D2 scan request, E1 data-plane setup + TXQ-table sync |
| tools/phase_matrix.py | one command runs a selected bring-up phase (boot/C2/D1/D2/E1) through bench_session.py, per-phase evidence dir + `phase-matrix.json`/`PHASE-MATRIX.md`; dry-run by default, real run needs --yes-really --attended |
| tools/bringup_report.py | aggregates phase-matrix evidence into the bring-up state table (boot/ready/calibration/vap/scan/dataplane) + `BRINGUP-STATUS.md` + JSON; never contacts the device |
| tools/fw_patch_builder.py | patched-firmware builder/verifier: pinned-source check, PD/PD2/PD3/PD3B recipe application (all ../wifi-irq-unmask/*.recipe.json auto-discovered), per-window sha16, capstone decode check, image-header protection (entry words + RAZ-only exception-record area), full-image verify of an existing patched copy, manifest |
| tools/bench_guards.py | the safety policy library shared by both tools (forbidden CA, CECE latch, release magic, devmem census, module/fw validation, static script audit, tamper-evident audit log) |
| tools/selftest.py | device-free unit/integration selftest, 312 checks |
| tools/bar0read.c | tiny READ-ONLY static ARM /dev/mem mmap helper (built on the host from this source, deployed as /root/hi5622-bar0read): the image has no busybox devmem applet, cannot read() PCI MMIO through /dev/mem on ARM, and PCI resource0 has no read(), so a PROT_READ mmap helper is the only reliable raw BAR0 read path |
| tools/run-selftest.sh | wrapper: python3 tools/selftest.py |

## Quick start (host only, no device)

    cd opensource/docs/incoming/wifi-bench-tooling
    ./tools/run-selftest.sh                      # 312/312 PASS
    python3 tools/bench_session.py --list-sequences   # bundled protocol sequences
    python3 tools/phase_matrix.py --list-phases       # the bring-up ladder
    python3 tools/bench_session.py --list              # modes (incl. cfg80211)

    # what would an attended session do? (prints every planned write + audit)
    python3 tools/bench_session.py --list

    # data-driven protocol sequence (PD4 boot + ctx_create + txq_sync72),
    # host-only dry-run against the real module/PD4 metadata
    python3 tools/bench_session.py --mode protocol --dry-run \
        --module /path/hi5622_wifi.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /path/wifi_block.bin \
        --ssh-host 192.168.1.1     # our image LAN; stock is 192.168.10.1
    # the static read helper is auto-built from tools/bar0read.c with the lab
    # cross compiler (pass --bar0read <prebuilt> to use an existing binary)
    python3 tools/bench_session.py --mode our-golden --dry-run \
        --module /path/hi5622_pciet_hcc.ko \
        --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt

    # patched-firmware build/verify against the LOCAL vendor blob (never committed)
    PY=../../../../.provision-venv/bin/python
    $PY tools/fw_patch_builder.py --list
    $PY tools/fw_patch_builder.py --build PD --build PD2 --build PD3 --build PD3B   # -> /tmp/hi5622-patched
    $PY tools/fw_patch_builder.py --verify-manifest /tmp/hi5622-patched/manifest.json

## Modes

| mode | instrument | what runs | writes |
| --- | --- | --- | --- |
| stock-golden | stock vendor stack, telnet | read-only golden-state capture | none (read-only) |
| our-golden | our instrument .ko | golden boot + D2H ready-pair check | 6 sysfs + 642 devmem + release |
| patched | our instrument .ko + FIRMWARE.<PATCH>.bin (PD2/PD3/PD3B) | same boot on a patched image + PD/PD2/PD3 observables | same as our-golden + fw deploy |
| h2d | our instrument .ko | our-golden then exactly ONE hcc_sr_announce | +1 announce |
| protocol | hi5622_wifi .ko + PD4-patched fw + PHY block | PD4 golden boot, strict ready pair, then the data-driven sequence (ctx_create t1 -> PACK -> txq_sync72 -> PACK -> D2H delta) with per-step gates + JSON/markdown evidence | prepare/upload/verify/seed/hcc_arm/hcc_loop/release + 2 H2D writes |
| cfg80211 | hi5622_wifi (cfg80211 build) .ko + PD4 fw + PHY block + cfg80211.ko + iw + libnl-tiny | protocol mode plus: deploy/load cfg80211.ko BEFORE the instrument, /tmp userspace (iw + libnl-tiny), strict ready pair, wiphy-registration gate (/sys/class/ieee80211 + iw phy), then a cfg80211/VAP/scan sequence with `shell` steps | same boot writes + support-file deploys + the sequence's own iw actions |

Real attended command (needs a human at the bench, recovery door armed,
read-only UART capture running):

    python3 tools/bench_session.py --mode our-golden --yes-really --attended \
        --module /root-on-host/hi5622_pciet_hcc.ko \
        --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt

    # the protocol lane's ordered lap on the PD4-patched firmware
    python3 tools/bench_session.py --mode protocol --yes-really --attended \
        --module /root-on-host/hi5622_wifi.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --wifi-block-bin /path/wifi_block.bin

    # patched-firmware session on the pd2 lane's corrected-context patch
    python3 tools/bench_session.py --mode patched --yes-really --attended \
        --module /root-on-host/hi5622_pciet_hcc.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD2.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --fw-path hi_wifi/FIRMWARE.PD2.bin \
        --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt

Watch mode = add --print-script / --print-plan to see the exact device-side
script and plan before running; --json prints the machine-readable summary.

### SSH transport, the raw read path, and the reboot gate (first live fire fixes)

The first live fire of the protocol mode (2026-11-25 06:23 UTC, evidence
build/evidence-bench-gates/wifi-bench/20261124T062301Z-protocol-6f974fe1)
exposed three host-side issues, all fixed here and re-verified device-free:

1. **File transfer.** The lab dropbear rejects the legacy scp protocol, so
   put/get no longer use tools/rscp.sh.  Both go through tools/rsh.sh with the
   project convention put = cat local | rsh 'cat > remote' (stdin-streamed,
   binary-safe) and get = rsh 'cat remote' > local.  The SSH transport now
   pins ROUTER_HOST from --ssh-host (preferred) or the ROUTER_HOST env var and
   normalises a bare IP to root@IP.  Our image normally answers at
   root@192.168.1.1; the stock vendor LAN is root@192.168.10.1.  The plan and
   preflight show the exact target, and warn when neither is supplied.
2. **Raw BAR0 reads.** The 2026-11-25 fire emitted RD32 lines with EMPTY
   values: the generated script read dd if=/sys/.../resource0, which this
   kernel does not support for read().  The protocol mode now deploys the
   static READ-ONLY helper tools/bar0read.c (ELF32 ARM ET_EXEC, sha-verified,
   chmod +x, refuses unaligned addresses and the RC misc window) and reads
   through a PROT_READ /dev/mem mmap at the BAR0 physical base from
   /sys/.../resource.  A readpath self-check reads BAR0+0 BEFORE the boot
   ladder and stops the session if the read path is broken; the host-side
   evidence distinguishes an EMPTY device read (device-read-empty) from a
   missing label, and a pre-sequence stop marks every sequence step
   NOT-RUN instead of pretending step 1 failed.
3. **--reboot before/after.** The old wait accepted the PRE-reboot uptime as
   "back" and raced the deploy during shutdown.  The hardened wait samples
   the pre-reboot uptime first, accepts only an observed-down-then-up, an
   uptime that went backwards, or a very fresh (<=8 s) uptime, requires a
   second confirming read, waits up to 300 s, and the deploy path retries
   only IDEMPOTENT host->box operations (mkdir/put/sha256sum) across a
   transient link drop - the session script itself is still fired exactly
   once (never retried).

## What a run produces

Evidence dir (gitignored build/evidence-bench-gates/wifi-bench/<session-id>/,
or build/wifi-bench/ when writable):

| file | content |
| --- | --- |
| session-plan.json | mode, module/fw/block hashes, every planned write, audit verdict |
| device-script.sh | the exact device-side POSIX script that ran |
| preflight.txt | host gate results (bench link, recovery door, UART, TFTP, lock) |
| report.txt | raw structured session report (sections + machine lines) |
| summary.json | parsed summary: result/stop rule, page/state/mask/out0-5, ready pair, rd_catalog, hcc_events, classifier |
| writes-host.jsonl | sha256-chained write audit (planned + host actions) |
| writes-device.log | per-write log from the box (fetched after the run) |
| REPORT.md | human-readable session report |
| protocol-plan.json | protocol mode: the validated sequence plan (steps, checks, pins, totals) |
| protocol-evidence.json | protocol mode, real run: hi5622-protocol-evidence/1 - per-step/per-check host verdict + captured value + device gate verdict, boot/mailbox observables, rd32 map, session/module/firmware hashes |
| PROTOCOL-SEQUENCE.md | protocol mode, real run: step table, every check with its actual value, boot observables and the failure triage block |

## Safety guards encoded (see REPORT.md section 4)

- never write CA 0x400392F0 (out[5]); never read the RC misc window 0x10161000;
- release is 0x5A5A -> CA 0x40000108 only; CECE default cece_mode=1 (never),
  and any CECE write is refused unless a real handshake-page +8==2 latch was
  observed (latch-once semantics survive the PD page clobber);
- at most ONE H2D announce per session; a timeout closes the lane, no retries,
  payload frames refused unless explicitly allowed;
- custom-block devmem writes are only possible inside the two firmware block
  windows (platform 0x4086A800+0x60, wifi 0x4086A860+0x9A8);
- fw_path must be a RELATIVE kernel firmware name (the 2026-11-24 PD run lost
  a run to an absolute path); module must be allowlisted ARM32 ET_REL, GPL,
  vermagic 6.18.60 SMP ARMv7;
- image-header protection: file 0x00/0x04 are the live entry A/B words and are
  never patchable; the reserved exception-record area 0x08..0x4F is patchable
  only while the pinned source bytes there are all zero (it is RAZ in the
  vendor image); the live exception handler table from 0x50 is never patchable;
- the generated device script is statically audited before it is deployed
  (forbidden addresses, non-WO attribute stores, UART writes, rmmod, retry
  loops, unguarded CECE, unresolved devmem bases) and must parse with sh -n;
- host preflight must PASS: bench link, recovery door READY for write modes,
  read-only UART capture running, no write-enabled TFTP server, no other bench
  session, --attended, evidence dir gitignored/writable;
- the patched firmware file must be byte-for-byte reproducible from a pinned
  vendor source + recipe and be listed in a builder manifest (sha256 match);
- every deployed file is sha256-verified on the box BEFORE the session script
  runs; every write is logged.

Protocol mode adds these guards on top of the list above:

- the sequence JSON is validated fail-closed: unknown schema fields, step
  kinds, attributes, expectation keys, duplicate labels, forward pointer
  references, unaligned dynamic adds and reads outside the six inbound BAR0
  viewports are refused before anything is deployed;
- the module must be named in the sequence's module_names and the patched
  firmware sha256/length must match the sequence's PD4 pin; insmod parameters
  must be real module parameters and h2d_send_budget must satisfy the
  sequence's one-frame-per-idea minimum (never raised mid-session);
- the protocol plan contains only prepare/upload/verify/seed,
  hcc_arm/hcc_loop, release 0x5A5A and the sequence's own H2D triggers - no
  devmem writes, no CECE, no announce; the generated script is statically
  audited and must parse with sh -n;
- dynamic (base+offset) reads are resolved through the endpoint inbound
  regions at runtime; the script stops before the read if the pointer matches
  no convention or resolves outside the 16 MiB BAR0 window;
- the device script stops on the FIRST failed gate (no retries), and the host
  re-evaluates every gate from the captured report independently.

## Tests

    ./tools/run-selftest.sh        # 503/503 device-free checks (task-114: +191)
    $PY tools/fw_patch_builder.py --selftest   # 27/27 (capstone decode checks)

The selftest covers arguments/refusals, patch application on synthetic data,
layered recipes, manifests/tamper detection, module ELF validation, the static
script audit (positive and negative cases), report parsing/classifier fixtures,
dry-run CLI for all five modes, a fake-transport end-to-end run (golden and
stop fixtures), host TFTP/UART/evidence checks, and the sha256 audit chain.
The +135 protocol checks cover: sequence-schema refusals (unknown/duplicate/
forward/unaligned/out-of-window/RC-misc cases), inbound-region pointer
resolution, protocol plan/audit/sh -n, the generated gate_num and norm_reg
shell functions executed under /bin/sh (full-mask string compare, byte masks,
16-bit ge, 8-hex normalisation), a synthetic protocol end-to-end execute()
PASS + read-gate FAIL + attr-pattern FAIL + pre-sequence readpath FAIL
(NOT-RUN accounting, device-read-empty, evidence artifacts, triage
rendering), the SSH transport (ROUTER_HOST pinning, bare-IP normalisation,
stdin put / byte-exact get / failure handling), the hardened reboot wait
(down-then-up, fast reboot, backwards uptime, stubborn old uptime, no false
PASS), the read helper (ELF validation, deployed sha in the script, helper
miss/build refusals, rd32 primary + /dev/mem fallback under /bin/sh), and the
protocol CLI (--list-sequences, dry-run, budget/parameter refusals).

## Not proven here

- The device-side shell has never been executed on hardware by this lane; it
  is validated by construction, static audit, sh -n, and a fake transport.
  The protocol-sequence automation mirrors the 2026-11-25 hardware-validated
  ordered lap.  The FIRST live fire (2026-11-25 06:23 UTC) validated the boot
  ladder end to end (insmod/prepare/upload/verify/seed/arm/loop/release and
  the strict ready pair all PASSED) but stopped at the first raw read because
  the resource0 dd path returns nothing on this kernel; the fixes above are
  host-only verified (no device contact) and the re-fire is the real
  validation of the sequence steps themselves.
- The first attended run of each mode is the real validation; start with
  stock-golden (read-only) and our-golden, record the evidence dir, then
  patched/h2d.
- build/ is root-owned on this checkout; the runner falls back to the
  user-owned build/evidence-bench-gates/wifi-bench/ tree and warns loudly if
  no project evidence dir is writable (/tmp is not evidence).

## 9. task-114: bring-up phase matrix, cfg80211 mode and the wave-2 ladder

Added 2026-11-25 (host-only, no device contact).  The lane now covers the whole
post-flash bring-up as *phases*, each a bundled data-driven sequence that runs
through the same one-command session runner:

| phase | sequence | mode | what it proves |
| --- | --- | --- | --- |
| boot/ready | bringup-boot-ready.json | protocol | golden page/state/mask/staging + strict ready pair, read-only |
| C2 | bringup-c2-calibration.json | protocol | full six-calibration boot: ctx_create type 1 -> txq_sync72 -> 6x cfg_cal 3502..3507 -> six 298 B g1 id3 answers (budget 8) |
| D1 | bringup-d1-vap.json | cfg80211 | wiphy registration gate + iw phy + netdev VAP add/del lifecycle (M0/M1) |
| D2 | bringup-d2-scan.json | cfg80211 | ONE nl80211 scan request + no-exception gate; D2H scan events watch-only |
| E1 | bringup-e1-dataplane.json | protocol | dp lane ladder D: dp_probe read-only MAC reachability + ONE dp_setup (rings/table/MAC program verified) + ONE key-0x0320 TXQ-table address sync + post pack |

New tools/modes:

- **bench_session.py --mode cfg80211** (also a new ModeSpec): validates and
  deploys cfg80211.ko (GPL/vermagic support-module policy) to /root, iw to
  /tmp/hi5622-bench-iw and libnl-tiny to /tmp; enforces cfg80211-BEFORE-
  instrument module order, selects the iw path (deployed copy else $PATH),
  sets LD_LIBRARY_PATH=/tmp, and after the strict ready pair waits for the
  driver's wiphy registration (up to --cfg-ready s) and gates
  cfg80211_status registered=1 + iw phy before the sequence.  Missing support
  artifacts refuse a real run; a dry-run records unverified placeholders and
  warns.
- **protocol_sequence.py** grows the allowlisted **shell** step kind (iw dev /
  iw phy / iw iface add|del / iw scan / ip link / cat /sys/class/... / dmesg -
  typed args, expect_rc, required and absent output patterns; iw only in the
  cfg80211 profile), the hi5622-wifi-cfg80211 profile, require keys
  (module_sha256/cfg80211/attrs/tools), present read gates, absent attr
  patterns, and relative D2H catalogue deltas (min_count_delta) plus exact
  last_total gates.
- **host-side capability probe**: every attr/token a sequence touches must be
  present as a C string in the target .ko; otherwise the plan is refused with
  module-attr-missing (BLOCKED in the matrix).  This is how a missing wave-3
  driver token becomes visible before bench time.  (Tiny synthetic fixtures
  skip the probe with a warning.)
- **tools/phase_matrix.py**: --phase boot|ready|C2|D1|D2|E1|all runs the
  phase through bench_session.py, writes per-phase evidence dirs plus
  phase-matrix.json/PHASE-MATRIX.md; --module-for PHASE=PATH selects a
  different module per phase (e.g. the cfg80211 build for D1/D2);
  --yes-really --attended for a real session, dry-run by default.
- **tools/bringup_report.py**: reads phase-matrix evidence and renders the
  boot/ready/calibration/vap/scan/dataplane state table (BRINGUP-STATUS.md +
  JSON), with a next action for every non-green row.  Never contacts the
  device.

Dry-run evidence (2026-11-25, real local artifact digests; full table in
**PHASES.md** section 6): boot/C2/D1/D2 **PLAN-OK** (5/28/11/11 steps with
18/54/24/15 checks; C2 8 device writes; cfg80211-first load order + wiphy gate
+ sh -n PASS) and E1 **BLOCKED-TOKEN** (the current calibration-token module
carries no h2d_txq_addr_sync C string - the dp-bringup lane's deliverable).
The rendered bring-up table is: boot READY | ready READY | calibration READY |
vap READY | scan READY | dataplane BLOCKED.

Live-fire fixes (REPORT.md 9.1/9.2/9.3): (a) the generated attr-pattern shell gates
had a corrupted pattern literal (hard-coded path instead of $p), so every
pattern gate failed on the first C2 fire - fixed, and the gates are now
whitespace-tolerant both device-side and host-side (wrapped long attr lines
still match), with a wrapped-capture regression test; (b) post-write gates
cat'ed the WRITE-ONLY trigger attribute (EACCES) - every write step now
captures the read-only status/events attr from the bench_guards
WO_ATTR_READBACK policy (h2d_* -> h2d_status etc.), attr_expect of a WO attr is
refused at plan time, and every bundled sequence is checked for WO reads;
(c) the C2 post-create gate translated the allocator return V through the
region-2 PKTRAM CA convention, whose host read is unvalidated/reads 0 - the
step now gates the HARDWARE-MEASURED ctx4 body PACK at absolute BAR0
addresses (0x7D4458=4, 0x7D4468=0x00010001, 0x7D488C=0x0100478C) with no
pointer conversion, and tests lock the addresses/values in; (d) the cfg80211
script expanded an unset LD_LIBRARY_PATH under set -u (D1 fire rc=2) - it now
sets the support dir explicitly, a static audit rejects any referenced-but-
never-assigned variable, and the iw preload section is executed under sh -u
with an empty environment in the selftest; (e) E1 now uses the
wifi-datapath-bringup driver surface (dp_probe/dp_setup/dp_status/dp_rings +
h2d_txq_addr_sync alias) and gates the dp ladder D end to end; (f) shell-step
output gates searched a single-quoted '$RDIR' path (never expanded) and the
host evaluator read the shell stdout from the wrong report section - gates now
pass "$RDIR/seq-<n>.out" double-quoted and each shell step owns a
'section seq-<n>' capture, with first-line/wrapped/genuine-miss and
control-line-exclusion tests plus a replay of the real 45-line fire capture;
(g) the host pattern matcher threw on literal non-regex fragments ('+0x38 = ...',
'*', '?') - it now falls back to literal substring matching and the real E1
fire report replays PASS; (h) shell steps support a documented BLOCKED outcome
(blocked_rc + blocked_patterns) so the D2 scan's -EOPNOTSUPP stub records
BLOCKED, the session continues and the phase matrix reports BLOCKED-DRIVER
(not ERROR); phase_matrix also gained --reboot-after/--reboot-between for
back-to-back phases (a loaded hi5622_wifi blocks the next phase); the first
full-matrix attempt hung 12+ min because the old reboot-after path issued a
plain synchronous 'reboot' with no per-call budget and the matrix killed only
the direct child - the reboot path is now hard-bounded and logged
(/sbin/reboot -f background + --reboot-deadline), matrix-owned between phases,
and phase children are killed as a process group on --phase-timeout (no
orphans).

Selftest: **312 -> 503** device-free checks (+191): shell-step refusal matrix,
shell/absent/present evaluation fixtures, synthetic C2 six-response pass +
wrong-last_total + missing-response negatives, synthetic D1 wiphy+VAP add/del
pass + leftover-netdev negative, cfg80211 mode plan/deploy/script (sh -n),
capability-probe refusals (module-attr-missing + sequence-profile), phase-matrix
end-to-end dry-runs (PLAN-OK and BLOCKED-TOKEN) and the bring-up report mapping.

Explicitly not proven: no phase was fired on hardware by this lane.  D2 is
BLOCKED-DRIVER today (the scan-ap lane's fail-closed -EOPNOTSUPP stub, rc=95)
and E1 is BLOCKED-TOKEN until the dp-bringup token lands; the sequences are
ready JSON edits when they do.  See PHASES.md section 7.
