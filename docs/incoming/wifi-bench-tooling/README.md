# wifi-bench-tooling - one-command bench sessions + patched-firmware builder/verifier

**Lane:** wifi-bench-tooling (shared task **task-97**), host-only, 2026-11-24.
No device was contacted while writing or testing this lane: every check in the
selftest runs on synthetic data, and every dry-run command below explicitly
refuses to touch the router. The attended session itself is the Lead's to run.

**Read first:** STATUS.md sections 3b/3c, the newest mem-entries.md entries
(2026-11-24 GP8/GP9/GP10/PD), and the lane reports this tool consumes:
wifi-irq-unmask/REPORT.md (PD + PD2 recipe), wifi-hcc-service/BENCH-PROCEDURE.md,
wifi-d2h-direct/BENCH-PROCEDURE.md, wifi-transport-port/build/ (sysfs inventory).

## What this is

| tool | what it does |
| --- | --- |
| tools/bench_session.py | one command for an attended Wi-Fi bench session: modes stock-golden / our-golden / patched / h2d; host preflight, deploy+sha read-back, module load with the fw_path variant, prepare/upload/verify, custom-block pre-seed, hcc_arm/hcc_loop, release-only, golden-page gate, ready-pair check, optional one-shot announce, structured report + sha256-chained write audit |
| tools/fw_patch_builder.py | patched-firmware builder/verifier: pinned-source check, PD/PD2 recipe application (PD2 auto-discovered from the wifi-irq-unmask lane), per-window sha16, capstone decode check, full-image verify of an existing patched copy, manifest |
| tools/bench_guards.py | the safety policy library shared by both tools (forbidden CA, CECE latch, release magic, devmem census, module/fw validation, static script audit, tamper-evident audit log) |
| tools/selftest.py | device-free unit/integration selftest, 175 checks |
| tools/run-selftest.sh | wrapper: python3 tools/selftest.py |

## Quick start (host only, no device)

    cd opensource/docs/incoming/wifi-bench-tooling
    ./tools/run-selftest.sh                      # 175/175 PASS

    # what would an attended session do? (prints every planned write + audit)
    python3 tools/bench_session.py --list
    python3 tools/bench_session.py --mode our-golden --dry-run \
        --module /path/hi5622_pciet_hcc.ko \
        --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt

    # patched-firmware build/verify against the LOCAL vendor blob (never committed)
    PY=../../../../.provision-venv/bin/python
    $PY tools/fw_patch_builder.py --list
    $PY tools/fw_patch_builder.py --build PD --build PD2     # -> /tmp/hi5622-patched
    $PY tools/fw_patch_builder.py --verify-manifest /tmp/hi5622-patched/manifest.json

## Modes

| mode | instrument | what runs | writes |
| --- | --- | --- | --- |
| stock-golden | stock vendor stack, telnet | read-only golden-state capture | none (read-only) |
| our-golden | our instrument .ko | golden boot + D2H ready-pair check | 6 sysfs + 642 devmem + release |
| patched | our instrument .ko + FIRMWARE.<PATCH>.bin | same boot on a patched image + PD/PD2 observables | same as our-golden + fw deploy |
| h2d | our instrument .ko | our-golden then exactly ONE hcc_sr_announce | +1 announce |

Real attended command (needs a human at the bench, recovery door armed,
read-only UART capture running):

    python3 tools/bench_session.py --mode our-golden --yes-really --attended \
        --module /root-on-host/hi5622_pciet_hcc.ko \
        --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt

    # patched-firmware session on the pd2 lane's corrected-context patch
    python3 tools/bench_session.py --mode patched --yes-really --attended \
        --module /root-on-host/hi5622_pciet_hcc.ko \
        --fw-file /tmp/hi5622-patched/FIRMWARE.PD2.bin \
        --fw-manifest /tmp/hi5622-patched/manifest.json \
        --fw-path hi_wifi/FIRMWARE.PD2.bin \
        --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt

Watch mode = add --print-script / --print-plan to see the exact device-side
script and plan before running; --json prints the machine-readable summary.

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

## Tests

    ./tools/run-selftest.sh        # 175/175 device-free checks
    $PY tools/fw_patch_builder.py --selftest   # 23/23 (capstone decode checks)

The selftest covers arguments/refusals, patch application on synthetic data,
layered recipes, manifests/tamper detection, module ELF validation, the static
script audit (positive and negative cases), report parsing/classifier fixtures,
dry-run CLI for all four modes, a fake-transport end-to-end run (golden and
stop fixtures), host TFTP/UART/evidence checks, and the sha256 audit chain.

## Not proven here

- The device-side shell has never been executed on hardware by this lane; it
  is validated by construction, static audit, sh -n, and a fake transport.
- The first attended run of each mode is the real validation; start with
  stock-golden (read-only) and our-golden, record the evidence dir, then
  patched/h2d.
- build/ is root-owned on this checkout; the runner falls back to the
  user-owned build/evidence-bench-gates/wifi-bench/ tree and warns loudly if
  no project evidence dir is writable (/tmp is not evidence).
