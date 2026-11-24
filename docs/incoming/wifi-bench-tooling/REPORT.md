# wifi-bench-tooling - session runner, patched-firmware builder/verifier, safety guards

**Lane:** wifi-bench-tooling (shared task **task-97**). **Date:** 2026-11-24.
**Status:** host-only deliverable complete and device-free tested. No device was
contacted while writing or testing this lane; no vendor binary/firmware bytes are
committed; patched firmware copies stay local (default /tmp/hi5622-patched).

---

## 0. TL;DR

1. **One-command bench sessions.** tools/bench_session.py takes a mode
   (stock-golden / our-golden / patched / h2d), runs the host safety preflight,
   deploys the instrument + firmware + custom blocks with sha256 read-back,
   loads the module with the fw_path variant, runs prepare/upload/verify,
   pre-seeds both custom blocks, arms the ETE rings, starts the HCC service
   loop, releases with 0x5A5A ONLY, gates on the golden handshake page
   (+8==2 latch), polls the g4 ready pair (2.0 s / 20.0 s), optionally posts
   exactly one H2D announce, and writes a structured report + sha256-chained
   write audit. Every write is enumerated, guarded and logged before anything
   is sent to the box.
2. **Patched-firmware builder/verifier.** tools/fw_patch_builder.py applies and
   verifies the PD recipe and the pd2 lane's PD2 recipe (auto-discovered from
   ../wifi-irq-unmask/PD2.recipe.json) against the pinned local vendor blob:
   per-window sha16, CPU-address capstone decode check, full-image verify of an
   existing patched copy, manifest. On the local pinned blob it reproduces:
   PD = f52a9322c575a8cd... (matches the 2026-11-24 16:10 ledger PD run),
   PD2 = 7a6918077af91601... (matches the pd2 lane's expected image sha and
   is byte-identical between both independent builders),
   PD3 = eea9f2ff9345dc8d... and PD3B = ef0683dbc1a8b3b7... (match the pd2
   lane's PD3/PD3B expected images). All committed *.recipe.json files in
   ../wifi-irq-unmask/ are auto-discovered; no vendor bytes are committed.
3. **Safety guards.** bench_guards.py encodes the hard rules from
   BENCH-FACTS/LESSONS and the 2026-11-24 GP8/PD sessions, and the generated
   device script is statically audited before deployment. The two GP8-era
   bugs (0xCECE during ROM boot; absolute fw_path) are structurally
   impossible for this tooling.
4. **Device-free selftest:** 177/177 checks PASS (tools/run-selftest.sh), plus
   27/27 for the builder's own --selftest with capstone.

---

## 1. Inputs and cross-lane sources of truth

| input | source |
| --- | --- |
| PD patch recipe (file 0x7610 + 0x916A, source sha16) | wifi-irq-unmask/tools/patch_builder.py (task-90) |
| PD2 corrected-context recipe (file 0x7432 + 0x916A) | wifi-irq-unmask/PD2.recipe.json (task-91, commit d3416f1) |
| corrected boot protocol: release 0x5A5A only; never 0xCECE during ROM boot; CECE only after +8==2; latch-once semantics | 2026-11-24 GP8/GP9/GP10/PD ledger entries + STATUS 3b |
| session order, gates and stop rules | wifi-hcc-service/BENCH-PROCEDURE.md, wifi-d2h-direct/BENCH-PROCEDURE.md |
| sysfs inventory and semantics (prepare/upload/verify/release, hcc_arm/hcc_loop, hcc_status/ready/events/irq_probe/rings, rd_*, module params, fw_path) | wifi-hcc-service/src/, wifi-d2h-direct/src/, wifi-transport-port/build/ |
| host gates (bench link, recovery door, UART capture, TFTP) and transports (rsh.sh/rscp.sh, telnet-exec.py) | tools/ |
| custom-block layout and counts | wifi-s1-cpu1/REPORT.md (platform block 24 words at phys 0x4086A800; wifi block 618 words at 0x4086A860), local evidence files only |

The builder is data-driven on purpose: PD2 was a moving target during this
lane, so the recipe is a JSON document with schema hi5622-fw-patch-recipe/1
rather than hard-coded bytes. It is picked up automatically when committed.

---

## 2. Session runner (tools/bench_session.py)

### 2.1 Modes

| mode | uses module | firmware | blocks | transport | announce | boot gate |
| --- | --- | --- | --- | --- | --- | --- |
| stock-golden | no | none | none | telnet | no | read-only, no writes at all |
| our-golden | yes | pinned vendor FIRMWARE.bin (on-box sha checked) | yes | ssh | no | strict golden page if --require-ready |
| patched | yes | FIRMWARE.<PATCH>.bin deployed from the builder + manifest | yes | ssh | no | informational (PD deviations expected; captured and classified) |
| h2d | yes | pinned vendor FIRMWARE.bin | yes | ssh | yes, exactly one | strict; announce only after golden page AND id1+id2 |

### 2.2 Pipeline (write modes)

1. **Host preflight** (all must PASS): tools/bench-link.sh; tools/recovery-card.sh
   READY (write modes); live read-only UART capture in build/uart-logs/; no
   TFTP server started with --allow-wrq and no repo-root serving pattern; no
   other bench_session.py; --attended; evidence dir gitignored and writable.
2. **Local validation** before any contact: module ELF (ARM32 ET_REL, GPL,
   vermagic 6.18.60 SMP ARMv7, allowlisted name hi5622_pciet*), firmware
   (vendor pin: 928,920 B / sha256 7fc87e20...311b; or patched file must match
   a builder manifest entry), custom-block files (counts 24/618, indices
   0..N-1, 32-bit values, all devmem addresses inside the two block windows),
   and a static audit of the generated device script.
3. **Optional reboot + wait** (--reboot before/after, attended only).
4. **Deploy + sha256 read-back on the box**: module -> /root/<name>.ko;
   patched firmware -> /lib/firmware/<relative fw_path> (vendor golden mode
   verifies the on-box FIRMWARE.bin instead of deploying); block files ->
   /tmp/hi5622-bench/<session-id>/; session script. Nothing runs until every
   remote sha256sum matches the local pin.
5. **Device preflight**: kernel/expected mode, our modules absent (stock mode:
   vendor modules present), firmware size+sha256, endpoint + resource0,
   sha256sum/devmem availability, dmesg mark.
6. **Boot sequence**: insmod with fw_path=<variant> [poll_ms=0];
   prepare; upload; verify (gates on windows=1 / fw_loaded=1 / dmesg diffs=0);
   custom-block pre-seed with per-word readback; hcc_arm (armed=1);
   hcc_loop (loop=running, started BEFORE release so the flush window is open);
   release (0x5A5A only).
7. **Boot gate**: handshake page read at t=0.4 s and t=1.4 s (raw resource0
   reads, ack2 computed from +0x8), state/mask/out1; a real +8==2 latches the
   CECE policy (latch-once).
8. **Ready pair**: poll hcc_ready for id1 (<=2.0 s) then id2 (<=20.0 s);
   strict modes STOP on a missing pair, patched mode records it.
9. **Optional H2D** (h2d mode): requires page latch + id1 + id2, then exactly
   one store to hcc_sr_announce. The store is done with printf (NO trailing
   newline: the 2026-11-24 15:10 session lost a run to the exact-"1" grammar);
   timeout => "H2D-LANE CLOSED", no retry, out[0]/out[2]/out[5] captured.
10. **Observe/report**: hcc_status, hcc_ready, hcc_events, hcc_irq_probe,
    hcc_rings, rd_status, rd_decode, rd_catalog, dmesg tail, writes count;
    then SESSION_RESULT line. Stock-golden mode instead reads the golden-trace
    register set (BAR0 offsets) and stack/dmesg/iwinfo, all read-only.

### 2.3 Report and artifacts

The report is a line-oriented text format parsed by the runner: PAGE, RD32,
GATE, READY, BLOCK, H2D, STOP, SESSION_RESULT plus "== SECTION" blocks. The
runner derives a summary with: result/stop rule, page words + ack2, state,
mask + named exception bits (undefined/svc/prefetch-abort/data-abort), out0-5,
g_armed/g_pending, ETE status, CECE write counters, ready pair (id1/id2 count,
payload), D2H catalogue (g4 id1/id2/EXCP_INFO/frames, layout/overruns), block
readback, H2D result, failed gates, and a classifier block
(page_golden/page_ack2/page_bit14/page_clobbered/state_1/mask_zero/out1_bits/
ready_pair/rd_catalog_ready/h2d_consumed/exception_path_ran). The piecewise
audit is written as JSONL with a sha256 chain (verify_audit_chain) so a removed
or edited write record is detectable.

Artifacts go to a gitignored evidence dir: build/wifi-bench/<session-id>/ when
writable, else build/evidence-bench-gates/wifi-bench/<session-id>/ (this
checkout has a root-owned build/, so the fallback is the practical one); a
/tmp fallback prints a loud warning because /tmp is not project evidence.

### 2.4 Concrete dry-run evidence (no device contacted)

    python3 tools/bench_session.py --mode <mode> --dry-run --module <real d2h .ko> \
        --plat-block <synthetic 24-word file> --wifi-block <synthetic 618-word file>

| mode | planned writes | script audit | notes |
| --- | --- | --- | --- |
| stock-golden | 0 (read-only) | PASS (0 devmem sites, 0 attr, 0 announce) | telnet transport, no module |
| our-golden | 648 = 6 attr + 642 devmem | PASS (2 devmem loop sites, 6 attr, 0 announce) | insmod fw_path=hi_wifi/FIRMWARE.bin poll_ms=0; vendor fw sha checked on box |
| patched | 648 = 6 attr + 642 devmem | PASS | real PD2 image 7a6918077af91601... deployed to /lib/firmware/hi_wifi/FIRMWARE.PD2.bin, manifest-checked |
| h2d | 649 = 7 attr + 642 devmem | PASS (1 announce store) | announce gated on PAGE_LATCHED and H1==H2==1 |

(The block files in the dry-run above are synthetic; the real firmware block
data is local evidence and is not quoted anywhere in this deliverable.)

The generated device script (244 lines for our-golden) parses with sh -n (part
of the selftest) and is statically audited by bench_guards.audit_device_script:
literal devmem addresses must fall inside the block windows, loop-form devmem
bases are resolved from their literal constants (unknown => violation), CECE
may only appear inside a cece_gate block, second announce stores, non-WO attr
stores, UART writes, rmmod and retry loops are violations.

---

## 3. Patched-firmware builder/verifier (tools/fw_patch_builder.py)

### 3.1 Recipe schema (hi5622-fw-patch-recipe/1)

    {
      "schema": "hi5622-fw-patch-recipe/1",
      "name": "PD2",
      "about": "...",
      "source": {"sha256": "7fc87e20...311b", "len": 928920},
      "based_on": null,            // or another recipe name (layered patches)
      "segments": [
        {"file": 0x7432, "len": 62, "source_sha16": "...",
         "patched_hex": "...", "isa": "thumb",
         "expect": "canonical capstone decode at CPU = file + 0x40000"}
      ]
    }

Validation before a byte is written: source pin (sha256+len), per-segment
bounds/overlap/length and image-header policy, per-window source sha16 against
the CURRENT image (so layered recipes must hash their parent's output), capstone
decode comparison at the CPU address, unchanged image length.

Image-header policy (narrowed 2026-11-24 for PD3 after independently checking
the pinned image): file 0x00/0x04 are the live entry A/B words and are always
refused; file 0x08..0x4F is the reserved exception-record area (the pinned
image has zeros there and the first live handler word is at 0x50) and is
patchable only while the source bytes are all zero; any segment that would
cross from the reserved area into the live handler table is refused. This lets
the pd2 lane's PD3 use the only file-backed region with no branch target and no
PC-relative literal load, while entry A/B and the handler table stay protected. Files are only
written outside the git repositories (repo-rooted /docs or /opensource output
is refused; gitignored build/ requires the explicit --allow-repo-build flag).
A manifest (hi5622-fw-patch-manifest/1) records the source pin, each build's
chain, segments, warnings, output sha256/len and verified=true. --verify
rebuilds the recipe in memory and compares the whole 928,920 B image;
--verify-manifest re-checks every build listed.

### 3.2 Reproduced results on the local pinned blob (host-only, local read in place)

| recipe | segments | output | sha256 | cross-check |
| --- | --- | --- | --- | --- |
| PD | file 0x7610 len 6 + 0x916A len 4 | FIRMWARE.PD.bin (928,920 B) | f52a9322c575a8cd3a24c232d98586879a79edeeb8d6c16cc1aad8d506e1435d | matches the ledger's 2026-11-24 16:10 PD bench image prefix f52a9322 |
| PD2 | file 0x7432 len 62 + 0x916A len 4 | FIRMWARE.PD2.bin (928,920 B) | 7a6918077af91601f00901ad46b47f5ea36956b03faeda9d742ecc384a1b7728 | matches the pd2 lane's expected image sha exactly; independently reproduced byte-identically by both builders (wifi-irq-unmask/REPORT-PD2.md section 8, opensource 5e64bfd) |
| PD3 | file 0x0C len 64 (reserved exception-record area) + 0x916A len 4 | FIRMWARE.PD3.bin (928,920 B) | eea9f2ff9345dc8d16badd65e77a5f8b82cf58740b7203239838259adc3905ce | matches the pd2 lane's expected PD3 image sha; header window verified RAZ in the pinned source, entry words and 0x50 handler table untouched (REPORT-PD3.md section 3.3) |
| PD3B | file 0x916A len 8 + 0xCC1B0 len 4 (data word) | FIRMWARE.PD3B.bin (928,920 B) | ef0683dbc1a8b3b74e99f27111105538e8a437e70a8fad2a9103ae291cb745f2 | matches the pd2 lane's expected PD3B image sha (no-cave event-raise alternative) |

All four are byte-for-byte verify-PASS against their recipes and manifest
(--verify-manifest: 4 build(s), 0 failure(s)). Builder fixes made while
integrating the pd2 recipes: capstone decode must run at the CPU address
(file + 0x40000) for the recipe expect strings; the real 6.18.60 .ko carries
parameters as parmtype=name:type (not parm=), so the module validator accepts
both forms; and the image-header guard was narrowed from a blanket file < 0x40
refusal to the entry-word / RAZ-only-reserved-area / live-handler-table policy
needed by PD3 (see 3.1).

PD2-A bench context (REPORT-PD3.md sections 1-2, recorded here for the bench
hand-off): the PD2 cave at CPU 0x47432 (entry A config-3 block) is LIVE on our
boots - the bench run ended at page word 0x6F, i.e. entry A died before its
handshake - while PD1 (same epilogue hunk, no cave) booted golden. PD3 is the
same guarded poll stub relocated to the unreachable reserved header area and is
the recommended next patched-firmware session; PD3B is the no-cave event-raise
alternative for the event-controller question.

---

## 4. Safety guards (the hard rules, and where each one is enforced)

| rule | enforcement |
| --- | --- |
| never write CA 0x400392F0 (out[5]) | host write-guard (check_ca_write/check_devmem_write), static script audit, device script has no such write (it only reads out5 via resource0/hcc_status), module asserts it too |
| never read RC misc window 0x10161000 | host read guard + script audit forbidden-read scan |
| release 0x5A5A -> CA 0x40000108 only | write-guard release-magic check; release step is the only CA write in the plan |
| never write 0xCECE before page +8==2 | default cece_mode=1 (never); any CECE write requires a real +8==2 observation; latch-once survives the PD page clobber; the normal session plans no CECE write at all; audit rejects raw CECE devmem and ungated hcc_cece_kick |
| CECE kick limit 3/module load | CeceLatch.max_kicks |
| no H2D retries; one frame, one idea | H2dGate allows exactly one announce; timeout/consumed/refused close the lane; second planned/audited store is a violation; payload frames refused unless explicitly allowed |
| custom-block writes only in the two firmware windows | devmem census (platform 0x4086A800..0x4086A85F, wifi 0x4086A860..0x4086B207), host guard + script audit including loop-form address resolution |
| fw_path is a RELATIVE kernel firmware name | validate_fw_path (absolute path refused: the 2026-11-24 PD lost run), deploy prefix /lib/firmware enforced |
| module allowlist / one endpoint binder | ELF32 ET_REL ARM, GPL, vermagic 6.18.60 SMP ARMv7, name hi5622_pciet / hi5622_pciet_hcc / hi5622_pciet_b3, no vendor modules loaded |
| patched image is the verified one | builder manifest sha256+len match; full-image re-verify; deploy + on-box sha256 read-back before the session |
| UART read-only | no tty writes in the plan/script; audit UART-write scan; host preflight requires a read-only capture |
| no rmmod after release | rmmod never appears in the script (audit violation); power-cycle is the reset |
| every write logged | planned writes enumerated in the plan; device writes to writes.log; host actions to a sha256-chained JSONL audit |
| recovery door / attended | host preflight requires tools/recovery-card.sh READY for write modes and --attended; no write session can start otherwise |
| evidence discipline | evidence dir must be gitignored (build/...) or outside the repos; /tmp fallback is loudly flagged as not-evidence; patched firmware output inside the repo is refused |

---

## 5. Device-free evidence (how to reproduce)

    cd opensource/docs/incoming/wifi-bench-tooling
    ./tools/run-selftest.sh
    -> wifi-bench-tooling selftest: 177/177 checks PASS

    PY=../../../../.provision-venv/bin/python   # has capstone
    $PY tools/fw_patch_builder.py --selftest
    -> fw_patch_builder selftest: 27/27 PASS

    $PY tools/fw_patch_builder.py --build PD --build PD2 --build PD3 --build PD3B --out /tmp/hi5622-patched
    $PY tools/fw_patch_builder.py --verify-manifest /tmp/hi5622-patched/manifest.json
    -> PASS PD:   sha256 f52a9322...
    -> PASS PD2:  sha256 7a691807...
    -> PASS PD3:  sha256 eea9f2ff...
    -> PASS PD3B: sha256 ef0683db...
    -> verify-manifest: 4 build(s), 0 failure(s)

The 177 checks cover: forbidden CA/devmem census/release/CECE latch and clobber
semantics/H2D gate; fw_path and firmware pin validation; ELF module validation
(including non-ARM, license, name, vermagic, missing fw_path); attribute
policy; static script audit positive+negative cases (forbidden addresses,
forbidden reads, UART, rmmod, retry loops, raw CECE, ungated kick, two
announces, read-only stores, unresolved loop bases); audit chain tamper
detection; TFTP/UART/evidence-dir host checks; builder synthetic apply/verify/
manifest/refusals/tamper detection and CLI dry-run; plan generation for all
four modes (write census, release-only, no CECE, one announce, insmod fw_path
variant, sh -n on the generated script); report fixture parsing/classifier
(golden ready-pair run, golden-without-pair, PD data-abort/page-clobber run,
STOP run); fake-transport end-to-end execution (golden PASS + STOP); dry-run
CLI; and the "write session without --attended is refused" gate.

---

## 6. Using it for the next attended session

1. Human at the bench; recovery door armed; read-only UART capture running.
2. Build the instrument .ko (wifi-d2h-direct/tools/build.sh) and the patched
   image if needed ($PY tools/fw_patch_builder.py --build PD2).
3. Start with our-golden (record the evidence dir), then a patched session as
   one idea: PD3 (recommended - guarded poll stub in the unreachable header
   record area; PD2's cave is live on our boots, see 3.2) or PD3B (no-cave
   event-raise alternative). Deploy with --fw-file /tmp/hi5622-patched/FIRMWARE.PD3.bin
   --fw-manifest /tmp/hi5622-patched/manifest.json --fw-path hi_wifi/FIRMWARE.PD3.bin.
   PD2 pass/fail observables are in wifi-irq-unmask/REPORT-PD2.md section 5.5 and
   PD3/PD3B observables in REPORT-PD3.md: golden page/state/mask, then after
   exactly one H2D SR announce out[0] 0x8->0, out[5]=1, out[2]=8 - the patched/h2d
   modes capture exactly those readbacks in the report.
4. After the run: read summary.json + REPORT.md; the classifier already names
   the expected PD observables (data-abort bit, page bit14 missing, page
   clobber, out[1] bit2) and the h2d result. Power-cycle after a PD image run
   (--reboot after is supported but must be attended).
5. Suggested ledger row: mode, session id, module/fw sha256, page/state/mask,
   ready pair, H2D result, stop rule, evidence dir.

---

## 7. Limitations / not proven by this lane

- The device-side shell was never executed on hardware here. It is validated
  by construction (template generated from the same constants as the plan),
  static audit, POSIX sh -n, and a fake transport end-to-end test. The first
  attended run of each mode is the real validation.
- The device commands assume the instrument's sysfs surface from
  wifi-hcc-service/wifi-d2h-direct and busybox tools (dd/hexdump-or-od/
  sha256sum/devmem/dmesg). A future merged driver (task-94) with a different
  attribute surface should be checked in dry-run first (module-profile gate
  stops the session if an attribute is missing).
- Root-owned build/ on this checkout forces the evidence fallback; the tool
  states that explicitly rather than pretending /tmp is evidence.
- The patched images are local evidence only (default /tmp/hi5622-patched);
  no patched file, block file, vendor byte or ROM byte is committed here.

---

## 8. task-108: data-driven protocol-sequence mode

Added 2026-11-25 (host-only, no device contact). The 2026-11-25 ledger gave
the project its first *protocol sequence*: PD4 golden boot -> ready pair ->
ONE context-create H2D (g0 id1 key 0x64, type 1) -> PACK -> ONE TXQ config
sync H2D (72-byte node) -> zero exceptions -> the firmware D2H responses
g1 id3 (36 B) and g1 id0x12 (40 B).  task-108 automates exactly that as data.

What was added:

- **bench_session.py --mode protocol**: PD4 boot skeleton for the
  hi5622_wifi driver (fw pin + PHY block deploy + sha read-back, insmod with
  fw_path / wifi_block_path / h2d_send_budget validated against the module's
  real parameters, prepare/upload/verify/seed, hcc_arm/hcc_loop,
  release-only 0x5A5A, strict golden-page + ready-pair gate), followed by the
  data-driven steps, final observable capture and the usual sha256-chained
  write audit.
- **tools/protocol_sequence.py**: schema hi5622-protocol-sequence/1 validator
  (fail-closed: unknown fields/kinds/attrs/expectations, duplicate labels,
  forward base references, unaligned adds, reads outside the six inbound BAR0
  viewports are refused at plan time), shell-gate + pointer-convention code
  generation, an independent host-side re-evaluation of every gate, and the
  evidence renderers (hi5622-protocol-evidence/1 JSON + PROTOCOL-SEQUENCE.md).
- **tools/sequences/ladder-c-ordered.json**: the bundled ladder (11 steps /
  62 checks / 2 H2D writes) mirroring bench ladder C8-C14 plus the measured
  D2H response delta.  Future messages are a JSON change plus, at most, one
  line in the attribute policy census - no runner code edits.
- **Pointer conventions**: the hardware-validated type-1 lap read
  ctx+0x190 = 0x01011448, a PKTRAM region-2 device CA - not a fw-RAM CPU
  pointer.  Dynamic reads therefore resolve pointers through the real
  endpoint inbound-region table (pktram/acp/fwram-ca/io/tcm/cpu) and stop
  before reading if a pointer matches no convention.  The old C11 note's
  V + 0x6B8000 formula only applies to fw-RAM CPU pointers; the bundled
  buffer reads are captured-only until those PKTRAM contents are measured.
- **Evidence**: protocol-plan.json, protocol-evidence.json (per-check host
  verdict + captured actual + device gate verdict, boot/mailbox observables,
  rd32 map, hashes, totals), PROTOCOL-SEQUENCE.md with the step table and the
  failure triage block; stop-on-first-failure means one boot yields one
  failing step and everything later is NOT-RUN.
- **Tests**: selftest 177 -> **312/312** checks after the first-live-fire
  follow-up (schema/strictness refusals, pointer resolution, plan/audit/
  sh -n, the generated gate_num/norm_reg functions run under /bin/sh,
  synthetic end-to-end execute() with PASS / read-FAIL / attr-pattern-FAIL /
  pre-sequence readpath-FAIL fixtures, SSH transport, reboot-wait semantics,
  read-helper validation + miss/build refusals, evidence artifacts/triage,
  CLI incl. --list-sequences/--bar0read and budget/parameter refusals).
  Host-only real-metadata
  dry-run: module 400,732 B sha256 774c4f1f..., PD4 image pinned
  cfe24bfb..., 11 steps / 62 checks / 9 planned attribute writes, audit PASS,
  sh -n PASS.

Limits: the automation's first live fire (see 8.1) validated the boot ladder
but exposed the raw-read/SSH/reboot issues now fixed; the sequence steps
themselves are still only host-verified.  See PROTOCOL-SEQUENCE.md for the
schema, exact commands and the failure triage table.

### 8.1 First live fire (2026-11-25 06:23 UTC): three host-side fixes

The Lead fired the new protocol mode on the PD4 image
(evidence build/evidence-bench-gates/wifi-bench/20261124T062301Z-protocol-6f974fe1).
The boot ladder and the strict ready pair all PASSED, then the session stopped
at the first protocol read: every RD32 line had an EMPTY value because the
generated script read dd if=/sys/.../resource0 and this kernel's PCI resource0
file does not implement read() for MMIO.  The host evaluator reported
read-missing for every sequence step - the sequence wiring was fine, the raw
read transport was not.  Three host-side fixes, device-free re-verified:

1. **Raw read path.** tools/bar0read.c is a tiny static READ-ONLY ARM helper
   (PROT_READ mmap of /dev/mem at the BAR0 physical base from
   /sys/.../resource; refuses unaligned addresses and the RC misc window
   0x10160000..0x1016FFFF).  It is auto-built from source with the lab musl
   cross compiler, deployed to /root/hi5622-bar0read, sha-verified, chmod +x'd,
   and probed by a readpath self-check on BAR0+0 BEFORE the boot ladder.  This
   is needed because the image has CONFIG_DEVMEM=y/STRICT_DEVMEM=n but no
   busybox devmem applet (and no od), ARM __va() cannot map 0x5xxxxxxxx device
   addresses for read(), and resource0 is mmap-only.
2. **SSH transport.** put/get now stream through tools/rsh.sh
   (cat local | rsh 'cat > remote' / rsh 'cat remote' > local) instead of
   tools/rscp.sh (the lab dropbear rejects legacy scp).  ROUTER_HOST is pinned
   from --ssh-host or the env var and a bare IP becomes root@IP; the plan and
   preflight display the target and warn when neither is set.  Our image is
   normally root@192.168.1.1; the stock vendor LAN default is
   root@192.168.10.1.
3. **Reboot gate.** The --reboot before/after wait now samples the pre-reboot
   uptime, accepts only an observed down-then-up, a backwards uptime or a very
   fresh (<=8 s) uptime, requires a second confirming read and waits up to
   300 s; deploy retries only IDEMPOTENT host->box operations (mkdir/put/
   sha256sum) across a transient drop - the session script is still fired
   exactly once, never retried.

Evidence honesty added with the fixes: an empty device read is reported as
device-read-empty (not "label missing"), capture-only reads that never
happened are MISSING (not PASS), and a device stop before the first sequence
step becomes pre_sequence_failure with every step NOT-RUN.  Selftest grew
258 -> 312 device-free checks (SSH transport, reboot semantics, helper
validation/handling, empty-read evidence, pre-sequence stop).

---

## 9. task-114: bring-up phase matrix, cfg80211 mode, wave-2 ladder

Added 2026-11-25, host-only, no device contact.  Extends the protocol-sequence
runner into the **bring-up ladder**:

1. **cfg80211 bench mode** (bench_session.py --mode cfg80211): validates and
   deploys the cfg80211 environment the new image lacks - cfg80211.ko
   (GPL + vermagic support-module policy, insmoded BEFORE the instrument),
   iw (static ARM ET_EXEC -> /tmp/hi5622-bench-iw) and libnl-tiny (ARM ET_DYN
   -> /tmp, LD_LIBRARY_PATH) - then after the strict ready pair waits for
   /sys/class/ieee80211/phy* (wiphy registration workqueue), gates
   cfg80211_status registered=1 and runs iw phy as a read-only enumeration
   gate.  Missing support artifacts refuse a real run; dry-runs warn with
   unverified placeholders.
2. **shell step kind** in protocol_sequence.py: one allowlisted userspace
   command (iw dev/phy/iface/scan, ip link, cat /sys/class/{ieee80211,net},
   dmesg) with typed args, expect_rc and required/absent output patterns;
   iw only in the new hi5622-wifi-cfg80211 profile.  The generated script
   captures rc + output, gates device-side (gate_rc/gate_out_has/
   gate_out_absent) and the host re-evaluates each gate.
3. **Schema additions**: require.module_sha256 / cfg80211 / attrs / tools;
   present read gates; absent attr patterns; relative min_count_delta plus
   exact last_total D2H catalogue gates.  Host-side capability probe: every
   attr/token a sequence touches must be a C string in the target .ko (real
   modules only, >= 4 KiB), else the plan is refused with module-attr-missing
   - a phase whose driver token is still pending shows as BLOCKED before any
   bench time.
4. **Bundled phase sequences** (tools/sequences/):
   - bringup-boot-ready (read-only golden page + ready pair, 5/18/0)
   - bringup-c2-calibration (six-calibration boot, 28/55/8, budget 8; six
     298 B g1 id3 answers gated by relative delta + last_total)
   - bringup-d1-vap (cfg80211 M0/M1 wiphy + netdev add/del, 11/24/0)
   - bringup-d2-scan (scan request rc=0 + no-exception gates, 11/15/0; D2H
     scan events watch-only - see cross-lane note below)
   - bringup-e1-dataplane (ready id2 ring-pointer seed + read-only MAC slot-5
     probe + key-0x0320 TXQ-table address sync, 11/22/3)
5. **tools/phase_matrix.py**: one command per phase (boot/ready/C2/D1/D2/E1/
   all), per-phase bench_session evidence dirs plus phase-matrix.json +
   PHASE-MATRIX.md; --module-for PHASE=PATH for phase-specific modules;
   dry-run default, real run needs --yes-really --attended.
   **tools/bringup_report.py**: aggregates the matrix into the
   boot/ready/calibration/vap/scan/dataplane state table (BRINGUP-STATUS.md +
   JSON) with a next action for every non-green row; never contacts the device.
6. **Dry-run evidence** (real local artifact digests, host-only): boot/C2/D1/D2
   PLAN-OK (5/28/11/11 steps; 18/54/24/15 checks; C2 8 planned writes; the
   cfg80211 plan deploys support-module/userspace/library, renders the
   cfg80211-first load order + wiphy gate and passes sh -n) and E1
   BLOCKED-TOKEN (the current calibration-token module 407,996 B 2d45e545 has
   no h2d_txq_addr_sync C string).  Rendered state table: boot/ready/
   calibration/vap/scan READY, dataplane BLOCKED.
7. **Tests**: 312 -> 503 device-free checks (+191): shell-step refusal matrix,
   shell/absent/present evaluation fixtures, synthetic C2 six-response PASS +
   wrong-last_total + missing-response negatives, synthetic D1 wiphy+VAP
   add/del PASS + leftover-netdev negative, cfg80211 mode plan/deploy/script
   (sh -n), capability-probe refusals, phase-matrix end-to-end dry-runs
   (PLAN-OK + BLOCKED-TOKEN) and bring-up report mapping.

Cross-lane prerequisites consumed here: the calibration-token module
(wifi-h2d-send, 407,996 B), the cfg80211 image/module (wifi-cfg80211 505,076 B
+ cfg80211.ko 2,328,548 B), the scan-ap driver wiring (task-113, 622,544 B,
fail-closed -EOPNOTSUPP until a provider builder is registered) and the
wifi-vap-scan frame recipes + ladder (task-110; g0 id7/id8/id9/id0xC frames).
D2's PASS gate is "scan request accepted + no firmware exception", with D2H
events watch-only because the firmware scan handler (CA 0x6494C) hardcodes VAP
lookup id 0 while the M1 VAP is host-only; a graceful no-VAP refusal is an
accepted frame-validity outcome.  E1's token name is the automation contract
(h2d_txq_addr_sync); a rename is a one-line sequence-JSON change.

### 9.1 First C2 phase fire: attribute-pattern gate fix + regression test

The first real matrix phase fire (C2, evidence
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T071150Z/C2) reached
the boot ladder cleanly - deploy/sha read-back, host preflight, module-order
gate PASS, service-ready PASS, module-profile PASS - and then STOPPED at
GATE load-budget ("pattern not found in h2d_status: budget=8") although the
captured h2d_status readout right above it contained
"H2D send path (real post-ready messages, one frame per idea): budget=8 ...".

Root cause (host tooling, not the device): the generated shell helper's grep
pattern literal had been corrupted at generation time - gate_attr(),
gate_attr_absent(), gate_out_has() and gate_out_absent() were grepping a
hard-coded repository path string instead of the passed pattern $p (an edit
artifact in the generator template, not a wrapping problem: the raw sysfs
capture was not wrapped).  Every attribute/output pattern gate in every
sequence would have failed for this reason; load-budget was simply the first
one to run.

Fixes:

1. **Generator**: all four helpers now use the real pattern argument and
   normalise whitespace before matching (tr newline/tab to spaces, tr -s ' ',
   then grep -q "$p"), so a long sysfs line that does arrive wrapped
   (80-column console/capture, fold-style pipe) is joined back to single
   spaces and a multi-token pattern still matches.  gate_out_has and
   gate_out_absent do the same for captured userspace output; gate_grep
   (dmesg gates) is normalised the same way.
2. **Host evaluator**: protocol_sequence._search_pattern_ws() matches the raw
   capture first (so anchored per-line patterns keep working) and then a
   whitespace-normalised copy; used by attr_expect patterns, absent patterns
   and d2h_catalog device patterns.
3. **Regression tests** (selftest 382 -> 398): generated helpers are extracted
   from a real plan script and executed under /bin/sh against a fake sysfs
   attribute file containing wrapped lines - a token on the wrapped line, a
   pattern spanning the wrap, the absent-gate PASS/STOP paths and a genuinely
   missing token all asserted; plus an end-to-end wrapped report through
   parse_report() + evaluate() (PASS, and wrong-value FAIL).  The tests also
   assert the generated helper uses $p and contains no hard-coded path.
4. **Replay of the actual failed fire**: the real report's captured
   h2d_status text now (a) passes the host pattern check and (b) passes the
   fixed generated shell gate executed directly on that captured content
   ("GATE load-budget PASS (attr=h2d_status)").

Re-validated after the fix: C2 dry-run PLAN-OK (28 steps / 55 checks /
8 writes, new gate_attr load-budget h2d_status "budget=8", sh -n PASS);
full matrix unchanged (boot/C2/D1/D2 PLAN-OK, E1 BLOCKED-TOKEN); selftest
503/503 PASS.  The manual C2 on hardware already passed (8/8 messages, six
298 B g1 id3 answers): this fix only brings the automation gate in line with
the captured attr format.

### 9.2 Second C2 fire: write-only trigger vs read-only post-write capture

After the pattern-gate fix the re-fire (evidence
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T071817Z/C2) ran the
boot ladder and pre-pack gates, wrote the context-create trigger, and then
STOPPED:

    GATE ctx-create-t1.write PASS
    == SECTION seq-ctx-create-t1-h2d_ctx_create
    cat: can't open '/sys/bus/pci/devices/0000:00:00.0/h2d_ctx_create':
    Permission denied
    READ-FAIL attr=h2d_ctx_create
    STOP:ctx-create-t1.p0: pattern not found in h2d_ctx_create: ctx_created=1

Root cause: the attr_write renderer captured the WRITE-ONLY trigger attribute
for its post-write pattern gates.  A WO sysfs attribute cannot be cat'ed back
(EACCES), so every attr_write step with patterns (ctx_create, txq_sync72, the
six cfg_cal triggers, the E1 dp token, ladder-c's two triggers) would fail the
same way; ctx-create-t1 was simply the first.

Fix (applied to ALL sequences, no per-sequence special cases):

1. **bench_guards policy**: WO_ATTR_READBACK maps every write-only trigger to
   the read-only status/events attribute that carries its observables
   (h2d_* -> h2d_status, hcc_sr_announce/cece/dr_republish -> hcc_events,
   hcc_arm -> hcc_rings, prepare/upload/verify/seed/release -> status,
   rd_poll/rd_advance -> rd_status), plus RO_ATTRS / attr_is_write_only() /
   attr_readback() helpers.
2. **schema**: attr_write gains an optional 'readback' (must be a known RO
   attr; default from the policy; unknown/WO readback is refused); attr_expect
   now refuses a WO attribute outright - a plan can never read a WO trigger.
3. **render/evaluate**: post-write capture renders
   'r_attr_as seq-<step>-<readback> <readback>' and host evaluation reads that
   section; the write check documents the readback attr, and a WO-attr capture
   does not satisfy it (attr-capture-missing).
4. **explicit documentation**: C2 (8 triggers), E1 (3) and ladder-c (2) now
   carry 'readback: h2d_status' in the sequence JSON, and bench_session adds
   the readback attr to the required-attribute profile check.
5. **tests** (selftest 398 -> 415): attr_expect-on-WO refusal, readback-is-WO
   refusal, unknown-readback refusal, default readback + rendered capture +
   host evaluation (RO capture PASS, WO capture FAIL), a scan of every bundled
   sequence for any r_attr_as of a WO attr, and per-sequence readback checks
   (C2's eight triggers, E1's three).

Replay of the real failed fire: the fixed generated post-write gate, executed
against an h2d_status capture containing the expected post-create state,
returns "GATE ctx-create-t1.p0 PASS (attr=h2d_status)" and "...
frames_sent=1 PASS"; the old script's line
'r_attr_as seq-ctx-create-t1-h2d_ctx_create h2d_ctx_create' is gone from the
new script (fresh C2 dry-run: r_attr_as seq-ctx-create-t1-h2d_status
h2d_status; grep for any r_attr_as of a WO attr: NONE; sh -n PASS).

Note on H1/H2: the pending-mask pack reading H1 with PD4 is expected - PD4 is
consume-only and the firmware responses arrive through the non-mailbox
(direct device group-A ring) path our reader uses; no sequence change needed.

### 9.3 Third C2 fire: post-create V-pointer translation -> measured ctx4 body PACK

After the WO-readback fix the fire (evidence
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T072242Z/C2) went
green through pre-pack, ctx-create-t1.write and ctx-create-t1.p0
(attr=h2d_status), then STOPPED:

    GATE post-create.post_ctx190 PASS raw=0x01011448
    PTR-CONV post-create.post_sig10 base=0x01011448 conv=pktram
    RD32 label=post-create.post_sig10 bar0=0x1e9458 v=0x00000000
    STOP:post-create.post_sig10: expectation failed: raw=0x00000000 want=0x00000064

Root cause: the sequence gated two create-buffer signatures by translating the
allocator return V (0x01011448) through the region-2 PKTRAM device-CA
convention.  That convention is right for device-CA pointers, but V's host read
translation is [U]: the ctx-create lane documents BAR0 = V + 0x6B8000 for
firmware-RAM CPU pointers (which overflows the 16 MiB BAR0 for V=0x01011448),
and the region-2 CA conversion lands in a different memory and reads 0.  The
signature values themselves (0x64/3/0x92A) are real, but the host address
mapping was never hardware-validated.

Fix (robust option (a) - use the HARDWARE-MEASURED body pack): the C2
post-create step now gates the ctx4 body at absolute BAR0 addresses, measured
twice in the successful manual laps and recorded in the wifi-h2d-send
BENCH-PROCEDURE post-create PACK:

| read | BAR0 | expect |
| --- | ---: | --- |
| post_ctx190 | 0x7D45E8 | non-zero (0x01011448 in the fire) |
| post_ctx4 | 0x7D4458 | 0x00000004 |
| post_ctx10 | 0x7D4468 | 0x00010001 |
| post_ctx434 | 0x7D488C | 0x0100478C |

read_pack already supports an explicit absolute 'bar0' read (the dynamic
base+offset form is optional), so no schema change was needed; the C2 sequence
no longer performs any pointer-conversion read, and the ambiguity is documented
in the step's 'about'.  This also keeps the post-create gate independent of the
allocator's internal addressing.

Tests (selftest 415 -> 419): the C2 positive fixture now uses the measured PACK
values; new checks assert the exact absolute BAR0 addresses, no base+offset
read remains anywhere in C2, the gated values match the ledger, and a wrong
body signature (ctx434 = 0x0100478D) fails at post-create.

Re-validated: C2 dry-run PLAN-OK (28 steps / 55 checks / 8 writes), full matrix
unchanged (boot/C2/D1/D2 PLAN-OK, E1 BLOCKED-TOKEN), selftest 503/503 PASS.

### 9.4 D1 fire: unset LD_LIBRARY_PATH under set -u (generated-script fix)

The first D1 (cfg80211 mode) fire advanced through every boot/support gate
(readpath, image, module-absent, module-sha, firmware-sha, wifi-block-sha,
cfg80211-files, cfg80211-load, module-order) and then the generated session
script exited rc=2 at:

    /tmp/hi5622-bench/.../session.sh: line 211:
    LD_LIBRARY_PATH: parameter not set

Root cause: the cfg80211 preload block exported
LD_LIBRARY_PATH=/tmp:$LD_LIBRARY_PATH; the script runs under set -u and
LD_LIBRARY_PATH does not exist in the remote dropbear environment, so the bare
expansion aborted the script after the successful gates (evidence
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T072832Z/D1).

Fix:

1. The generated script now sets the support dir explicitly:
   export LD_LIBRARY_PATH=/tmp (iw + libnl-tiny are deployed there), with no
   inherited-value expansion at all.  The line has a comment naming the fire.
2. New selftest checks (inside the cfg80211-mode tests):
   - generated script parses under sh -u -n;
   - it contains export LD_LIBRARY_PATH=/tmp and no bare $LD_LIBRARY_PATH or
     braced-LD_LIBRARY_PATH reference anywhere;
   - a static set -u audit (find_unset_expansions) parses every variable
     reference in the generated script and fails if the name is never assigned
     anywhere (catches this class of bug, not just this variable);
   - the iw/LD_LIBRARY_PATH preload section is extracted from the generated
     script and EXECUTED under /bin/sh -u with an empty environment
     (LD_LIBRARY_PATH unset), asserting rc=0, the selected iw path and
     LD_LIBRARY_PATH=/tmp.
3. Re-validated against the real artifacts: the D1 dry-run script line is
   'export LD_LIBRARY_PATH=/tmp'; unset audit returns []; sh -u -n PASS; the
   extracted preload block runs clean under sh -u with LD_LIBRARY_PATH unset.

### 9.5 E1: the wifi-datapath-bringup DP attribute surface

E1 was written against the wave-3 dp-bringup driver contract; the lane has now
delivered it, so E1 was rewritten to the lane's own ladder D (and its
sequences/ladder-d-datapath.json), with the DP attributes registered in
bench_guards:

- DP_ATTRS: dp_probe (RO one-word MAC-slot-5 reachability read), dp_setup (WO,
  the ONE ring-allocation + MAC band-0 program with read-back), dp_txq_sync
  (WO key-0x0320 hand-off) and its h2d_txq_addr_sync alias, dp_status and
  dp_rings (RO evidence); DP_WO_ATTRS: dp_setup/dp_txq_sync/h2d_txq_addr_sync.
- WO_ATTR_READBACK: dp_setup -> dp_status (setup_done=1 failed=0 mac_regs=14
  table_entries=8), dp_txq_sync/h2d_txq_addr_sync -> h2d_status
  (txq_addr_sent=1 ok=2 timeout=0 frames_sent=2); write policy accepts one
  payload-free '1' store for each and never writes dp_probe/dp_status/dp_rings.
- E1 sequence (13 steps / 42 checks / 3 writes, budget 2): ready id2 seed ->
  pre pack -> ONE ctx-create (h2d_status readback) -> dp_probe (RO attr_expect;
  value captured, not gated - the driver's own region-3 translation settles
  the earlier BAR0 0x402A000 question) -> dp_setup (patterns on the dp_status
  readback) -> dp_rings snapshot (+0x38/+0x3c=0x8000, +0x48=0xfffffcc1,
  tid 7 entry) -> pre-hand-off h2d_status gates (txq_addr_sent=0,
  frames_sent=1, txq_addr_sync=ok) -> ONE h2d_txq_addr_sync -> post pack
  (mask/staging 0, state 1, page golden, out0/out2 0) -> catalogue capture.
  No raw userspace MAC-window read, no pointer conversion, no devmem write.

Validated host-only: E1 dry-run with the lane's module (462,952 B sha256
8fc693ad...) is PLAN-OK (13/42/3); the capability probe finds all dp_* strings;
the full matrix with per-phase modules (--module-for E1=dp module) is
boot/C2/D1/D2/E1 all PLAN-OK.  The dp_setup MAC program has not been fired on
the device by this lane.

### 9.6 D1 fire #3: shell-output pattern gates searched the wrong buffer

The D1 re-fire with the LD_LIBRARY_PATH fix (evidence
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T073341Z/D1) loaded
the cfg80211 stack and passed every support gate, the wiphy gate and
cfg80211_status gates, then stopped at:

    STOP:phy-info.p0: pattern not found in shell output: Wiphy phy0

while the same report section shows the captured stdout beginning with exactly
"Wiphy phy0" (report.txt lines 1438-1460).  Two independent tooling bugs, both
in the shell-step path:

1. **Device gate read a non-existent file.**  The rendered gate call was
       gate_out_has 'phy-info.p0' '$RDIR/seq-phy-info.out' 'Wiphy phy0'
   The output path was single-quoted, so the shell never expanded $RDIR and
   grep looked at a literal '$RDIR/seq-...out' file that does not exist.  (The
   direct bench_session-generated cfg-iw-phy gate used double quotes and
   passed - that asymmetry was the tell.)  This is the same class as the
   earlier pattern-literal bug.
2. **Host evaluator read the wrong report section.**  The shell step's stdout
   was appended under the step-header section 'protocol-<n>', while the host
   evaluator reads the shell capture from 'seq-<n>'; host-side shell pattern
   checks could therefore never match (the device stop masked it).  Shell
   steps now open their own 'section seq-<n>' immediately before running the
   command, so the captured stdout (and the SHELL-RC line) live where the
   evaluator expects them; STEP/STEP-ABOUT/SHELL-CMD lines are additionally
   excluded from pattern matching, so a pattern that only appears in a step's
   documentation cannot false-PASS.

Fixes and tests (selftest 432 -> 443):

- rendered gate calls pass the output file as a DOUBLE-quoted expansion
  ("$RDIR/seq-<n>.out"); a test asserts the generated script contains no
  single-quoted '$RDIR path;
- rendered shell steps emit 'section seq-<n>' before the command; test asserts
  the section precedes the capture;
- the generated gate_out_has/gate_out_absent functions are extracted and run
  under /bin/sh -u against fixture files: pattern on the first output line,
  pattern spanning a wrap, absent-gate PASS and a genuine miss still STOP;
- host evaluator tests: first-line + wrapped pattern both match in a captured
  report section; a pattern present only in STEP-ABOUT lines does not match; a
  genuine host-side miss still FAILs;
- replay of the REAL captured output from the failed fire (45 lines,
  'Wiphy phy0' first): the fixed device gate prints
  'GATE phy-info.p0 PASS (shell)' and the host evaluator over the corrected
  report layout returns PASS for both phy-info.rc and phy-info.p0.

Re-validated: D1 dry-run script has 'section seq-phy-info' before the iw call
and 'gate_out_has 'phy-info.p0' "$RDIR/seq-phy-info.out" 'Wiphy phy0'';
sh -n/sh -u -n PASS; full matrix unchanged (boot/C2/D1/D2/E1 all PLAN-OK);
selftest 503/503 PASS.  The next D1 fire should reach the VAP add/del steps
(iw phy phy0 interface add wlan0 / iw dev wlan0 del).

### 9.7 E1 fire: host pattern matcher rejected literal (non-regex) fragments

The E1 fire (dp module 462,952 B sha 8fc693ad..., evidence
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T074035Z/E1) ran the
whole session PASS - dp_probe, dp_setup (14 MAC regs / 8 table entries),
dp_rings, the key-0x0320 hand-off and the post pack all device-green - but the
protocol verdict came back FAIL: step dp-rings-check reported
"invalid host regex: nothing to repeat at position 0" for its first three
patterns ('+0x38 = 0x00008000', '+0x3c = 0x00008000', '+0x48 = 0xfffffcc1';
'tid 7 entry' matched).

Root cause: the host evaluator compiled every pattern as a regular expression,
and patterns starting with '+', '*', '?', '{' (or otherwise invalid) raise
re.error instead of being treated as literal output fragments.  The device-side
gate uses grep (BRE), where '+' is literal, so only the host re-evaluation
disagreed.

Fix: _search_pattern_ws() now tries regex matching on the raw and the
whitespace-normalised capture and, on re.error, falls back to a literal
substring match on both.  This keeps real regexes working while literal output
fragments (the dp_rings '+0x..' lines) gate correctly.

Tests: literal patterns '+0x38 = 0x00008000', '+0x48 = 0xfffffcc1', '*', '?',
'(literal' and 'tid 7 entry' all match; a literal fallback still misses when
absent and still matches across a wrap; valid regexes ('^1$', 'phy[0-9] info')
still work.  Replay of the REAL E1 fire report through the fixed evaluator:
protocol verdict PASS, all four dp-rings-check patterns PASS, no non-PASS step.

### 9.8 D2 fire: documented BLOCKED-DRIVER outcome + phase-matrix classification

The D2 fire (evidence .../20261124T073908Z/D2) ran correctly to the scan:
cfg-status, vap-add rc=0, link-up rc=0 and scan-baseline all PASS, then
'iw dev wlan0 scan' returned the fail-closed cfg80211 M2 stub:
"command failed: Not supported (-95)", SHELL-RC scan rc=161 (=-95), the old
gate stopped the session (STOP:scan.rc) and the phase matrix reported the row
as ERROR.

Fixes:

1. **Shell steps support a documented blocked outcome**: optional
   'blocked_rc' (list of exit codes) + 'blocked_patterns' (output signatures).
   The generated script's blocked branch records 'BLOCKED <step> rc=...' and
   continues the session (SEQ_BLOCKED=<step>); the final line becomes
   SESSION_RESULT: BLOCKED:<step>.  An undocumented code with the wrong/missing
   signature still STOPs; a documented code without the signature also STOPs.
   The host evaluator marks the step BLOCKED with code blocked-driver (not
   FAIL), counts it in totals.blocked, sets blocked_step, and the verdict is
   BLOCKED.  bench_session exits 12 for BLOCKED (0 PASS / 10 STOP / 11 FAIL).
2. **D2 sequence**: the scan step declares blocked_rc [95, 161] and
   blocked_patterns ['Not supported', 'scan needs an H2D frame provider'];
   expect_rc stays 0 so the step turns PASS automatically when the
   wifi-vap-scan g0 id0xC provider builder lands.  The post-scan mask/staging
   pack, the catalogue watch and the VAP delete still run after a blocked scan.
3. **Phase-matrix classification**: real runs are classified from the session
   summary + protocol evidence instead of the exit code alone - BLOCKED (or a
   blocked_step, or a STOP whose evidence carries blocked-driver/rc=95/161)
   maps to BLOCKED-DRIVER with a blocked_reason; PASS stays PASS, STOP/FAIL
   stay STOP/FAIL.  Replay of the REAL D2 fire evidence now classifies as
   BLOCKED-DRIVER (was ERROR).
4. **Back-to-back phases**: phase_matrix grew --reboot-after and
   --reboot-between (real runs only): --reboot-between reboots the box after
   every phase except the last (bench_session --reboot after), because a loaded
   hi5622_wifi makes the next phase stop at module-already-loaded / insmod
   rc=255.  want_reboot_after() is unit-tested (dry-run never reboots).

Tests (selftest 443 -> 481): blocked_rc/blocked_patterns schema refusals;
rendered blocked branch executes under /bin/sh -u - with rc=161 + signature it
records BLOCKED and continues, rc=95 too, an undocumented rc still STOPs;
host evaluator BLOCKED verdict/blocked_step/totals/triage; rc-without-signature
FAILs; SESSION_RESULT: BLOCKED parsing; generated session emits the conditional
SESSION_RESULT; D2 ships the documented blocked outcome; phase-matrix
classify() maps BLOCKED/STOP/FAIL/PASS correctly; reboot-between/after helper
semantics.  Full matrix remains boot/C2/D1/D2/E1 PLAN-OK; D2 dry-run script has
the blocked case branch and the conditional session result; all sh -n / sh -u
-n PASS.

### 9.9 Full-matrix hang: the --reboot-after path was neither robust nor bounded

The first --phase all --reboot-between run (2026-11-25 15:14, evidence
build/evidence-bench-gates/wifi-bench-phase-matrix/20261124T081406Z) completed
boot and C2 (~3-4 min each), then the D1 child (bench_session ... --reboot
after) was still alive 12+ min later with the box uptime unchanged, its
evidence dir holding only writes-host.jsonl, and a child ssh stuck on a
"cut -d. -f1 /proc/uptime" poll.  The matrix parent's kill left the child
running (an orphan that could later reboot the box under the operator).

Audit of the old path:

- (a) It issued a plain synchronous 'reboot' over ssh.  On this image that can
  wait for the procd shutdown path (or return without resetting when procd is
  wedged); it is not the project's /sbin/reboot -f form.  The ssh call was
  capped at 15 s but the box never reset, so the wait polled the unchanged
  uptime.
- (b) The wait had a nominal 300 s deadline but no per-call budget: each poll
  could spend poll_s + up-to-15 s in ssh, and an ssh call that hangs in
  communicate() could outlive the loop; there was no progress output, so a
  stuck run was indistinguishable from a slow one.
- (c) --session-timeout only bounds the remote session-script ssh call, not the
  reboot wait, the writes.log fetch, or the child process as a whole; the
  phase matrix had a flat 600 s subprocess timeout and subprocess.run() killed
  only the direct child, leaving ssh grandchildren alive.

Fixes:

1. **bench_session.reboot_and_wait() rewritten, hard-bounded and logged**:
   samples the pre-uptime; issues
   'sync; (sleep 1; /sbin/reboot -f[|reboot]) >/dev/null 2>&1 & echo
   REBOOT-ISSUED' (immediate reset, background so the ssh call returns);
   prints '[reboot] ...' progress lines with elapsed/remaining on every poll;
   clamps EVERY transport call to the remaining overall budget; accepts 'back'
   only after seen-unreachable / uptime-went-backwards / fresh small uptime
   plus a confirming re-read; returns (False, diagnostic) at the deadline and
   records reboot_after_ok in the session summary; a clearly refused command
   fails immediately.  New CLI --reboot-deadline (default 300 s) and
   --reboot-poll (default 4 s).
2. **phase_matrix owns the between-phase reboot**: it no longer asks the child
   session to reboot; after a real phase it calls do_reboot() (the same bounded
   helper over SshTransport), logs progress, and on failure stops and marks all
   remaining phases NOT-RUN (never start the next phase on a stale box).
   Use --reboot-between (all but last) or --reboot-after (every phase).
3. **matrix child supervision**: phases run with a hard --phase-timeout
   (default 900 s, --phase-timeout to change) in a new process group; on
   timeout the WHOLE group is SIGKILLed so no bench_session/ssh orphan survives
   the matrix run (the exact failure mode above).
4. The child's session-level --reboot after remains supported for direct one-
   command use, but multi-phase runs should let the matrix orchestrate it.

Tests (selftest 481 -> 503): reboot wait with a fake transport - normal
down-then-up, fast reboot, backwards uptime, slow reboot, stubborn old uptime
(bounded), reboot-but-never-returns (bounded), refused reset command,
failed stability re-read; the reset command content/background form and
'[reboot]' progress logging; execute() with --reboot after against a box that
never returns (session PASS unaffected, reboot_after_ok=False, execute
bounded, immediate reset issued); matrix child timeout kills the process group
and a normal child returns normally; matrix child argv never carries
--reboot; do_reboot() success/failure via a monkeypatched bounded helper.
Real validation of the bound: reboot_and_wait against an unreachable host with
a 2 s deadline returns in 2.1 s (no bench contact), and the full dry-run matrix
(including --reboot-between) still validates all five phases (no reboot in
dry-run).
