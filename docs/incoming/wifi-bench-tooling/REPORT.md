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
   PD2 = 7a6918077af91601... (matches the pd2 lane's expected image sha).
3. **Safety guards.** bench_guards.py encodes the hard rules from
   BENCH-FACTS/LESSONS and the 2026-11-24 GP8/PD sessions, and the generated
   device script is statically audited before deployment. The two GP8-era
   bugs (0xCECE during ROM boot; absolute fw_path) are structurally
   impossible for this tooling.
4. **Device-free selftest:** 175/175 checks PASS (tools/run-selftest.sh), plus
   23/23 for the builder's own --selftest with capstone.

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
bounds/header protection/overlap/length, per-window source sha16 against the
CURRENT image (so layered recipes must hash their parent's output), capstone
decode comparison at the CPU address, unchanged image length. Files are only
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

Both are byte-for-byte verify-PASS against their recipes and manifest. Two
builder bugs found and fixed while integrating the pd2 recipe: capstone decode
must run at the CPU address (file + 0x40000) for the recipe expect strings, and
the real 6.18.60 .ko carries parameters as parmtype=name:type (not parm=), so
the module validator now accepts both forms.

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
    -> wifi-bench-tooling selftest: 175/175 checks PASS

    PY=../../../../.provision-venv/bin/python   # has capstone
    $PY tools/fw_patch_builder.py --selftest
    -> fw_patch_builder selftest: 23/23 PASS

    $PY tools/fw_patch_builder.py --build PD --build PD2 --out /tmp/hi5622-patched
    $PY tools/fw_patch_builder.py --verify-manifest /tmp/hi5622-patched/manifest.json
    -> PASS PD:  sha256 f52a9322...
    -> PASS PD2: sha256 7a691807...
    -> verify-manifest: 2 build(s), 0 failure(s)

The 175 checks cover: forbidden CA/devmem census/release/CECE latch and clobber
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
3. Start with our-golden (record the evidence dir), then patched PD2 / h2d as
   separate sessions (one idea per session). PD2 pass/fail observables are
   specified in wifi-irq-unmask/REPORT-PD2.md section 5.5: golden page/state/mask,
   then after exactly one H2D SR announce out[0] 0x8->0, out[5]=1, out[2]=8 - the
   patched/h2d modes capture exactly those readbacks in the report.
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
