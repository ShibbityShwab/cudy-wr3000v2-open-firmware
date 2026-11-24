# Protocol-sequence mode - data-driven H2D startup with per-step gates

**Lane:** wifi-bench-tooling (shared task **task-108**, extends task-97).
**Status:** host-only, device-free tested. No device was contacted by this
work; the attended session is the Lead's to run. The bundled sequence mirrors
the hardware-validated working lap (2026-11-25 05:10/06:00 ledger entries),
but this automation itself has **not** been fired on hardware yet.

The 2026-11-25 breakthrough made the Wi-Fi control plane a *sequence*: PD4
golden boot -> D2H ready handshake -> host->device context-create H2D (g0 id1
key 0x64, type 1) -> PACK verification -> host->device TXQ config sync H2D
(g0 id1 cmd 0xd8, 72-byte node) -> zero exceptions -> firmware D2H responses
(g1 id3 36 B, g1 id0x12 40 B).  `bench_session.py --mode protocol` runs that
sequence from one data-driven JSON file, with a PASS/FAIL gate on every step,
stop-on-first-failure, and a JSON + markdown evidence record.

---

## 1. Quick start (host only, no device)

    cd opensource/docs/incoming/wifi-bench-tooling
    ./tools/run-selftest.sh                              # 258/258 device-free checks
    python3 tools/bench_session.py --list-sequences      # bundled sequences + sha256

    # what would the protocol session do? (real module + PD4 image metadata,
    # no device contact, no deploy)
    python3 tools/bench_session.py --mode protocol --dry-run \
      --module /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/opensource/docs/incoming/wifi-h2d-send/src/hi5622_wifi.ko \
      --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
      --fw-manifest /tmp/hi5622-patched/manifest.json \
      --wifi-block-bin /tmp/blocks/wifi_block.bin

The dry-run prints the sequence table, the exact planned writes, the static
script audit and writes `protocol-plan.json` into the evidence dir.  The
current host-only result for the bundled sequence with real metadata:

- module `hi5622_wifi.ko` 400,732 B sha256 `774c4f1f...`, vermagic 6.18.60 SMP ARMv7;
- firmware `FIRMWARE.PD4.bin` 928,920 B sha256 `cfe24bfb...` (pinned in the sequence);
- PHY block 2,472 B (local evidence, never committed);
- 11 steps / 62 checks / 9 planned attribute writes, script audit PASS,
  `sh -n` PASS, devmem writes 0 (the driver's `seed` attribute owns the block).

## 2. The attended command (Lead only)

    python3 tools/bench_session.py --mode protocol --yes-really --attended \
      --module /root-on-host/hi5622_wifi.ko \
      --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
      --fw-manifest /tmp/hi5622-patched/manifest.json \
      --wifi-block-bin /tmp/blocks/wifi_block.bin

Preconditions are identical to the other write modes and enforced by the host
preflight: `tools/bench-link.sh` PASS, `tools/recovery-card.sh` READY, a live
**read-only** UART capture, no write-enabled TFTP server, `--attended`, and the
evidence dir writable. Then the generated device script:

1. insmod with `fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin h2d_send_budget=2`
   (the sequence's `load_params`; the module's declared parameters are checked, the
   one-frame-per-idea budget is refused if below the sequence minimum, and the
   PD4 image sha256 is pinned by the sequence);
2. `prepare` -> `upload` -> `verify` -> `seed` -> `hcc_arm` -> `hcc_loop` -> `release` (0x5A5A only);
3. strict ready gate (golden page, state 1, mask 0, g4 id1 <= 2 s, g4 id2 <= 20 s,
   phase READY_TXQ_SYNC);
4. the data-driven sequence (below);
5. final read-only capture of h2d_status, h2d_plan, hcc_events, hcc_ready,
   hcc_status, boot (includes the mailbox words), rd_status, rd_catalog,
   rd_decode, dmesg and the device write log.

The real run still needs a human at the bench and the recovery door armed:
**no agent runs it**.  `--print-script` prints the exact device-side script;
`--print-plan` prints the full JSON plan before anything is deployed.

## 3. Sequence schema `hi5622-protocol-sequence/1`

A sequence is a JSON document under `tools/sequences/` (bundled:
`ladder-c-ordered.json`).  Top-level fields:

| field | meaning |
| --- | --- |
| `schema` | must be `hi5622-protocol-sequence/1` |
| `name` | `^[a-z][a-z0-9-]{0,47}$`, unique |
| `about` | free text, shown in the plan/evidence |
| `profile` | must be `hi5622-wifi-h2d` (the hi5622_wifi H2D driver) |
| `module_names` | allowlist, default `["hi5622_wifi"]`; the runner refuses any other module |
| `require` | `ready_pair`, `boot_page` (bools), `fw_sha256` (64 hex), `fw_len`, `budget_min` (int) |
| `load_params` | insmod parameters merged with the auto `fw_path`/`wifi_block_path`; keys must exist in the module's modinfo; `--module-param KEY=VALUE` overrides and is validated the same way |
| `steps` | ordered step list (1..64) |

Step kinds (unknown keys are refused, so typos fail closed):

| kind | fields | behaviour |
| --- | --- | --- |
| `read_pack` | `reads`: list of `{label, bar0}` or `{label, base, add}`, optional `expect`, `about` | read-only BAR0 word reads with numeric gates; labels are session-global and must be defined before use |
| `attr_expect` | `attr`, `patterns` (list) | every pattern must match the attribute (device-side grep; host re-checks the captured text) |
| `attr_write` | `attr`, `value`, `patterns` (optional) | ONE write-only sysfs store (the H2D message trigger) + post-write expectations; a failed store stops the session |
| `catalog_snapshot` | - | capture rd_catalog + rd_decode as a delta baseline |
| `d2h_catalog` | `expect_entries`: `[{group, id, min_count}]`, `device_patterns`, `before` (snapshot step), `require_frames_increase` | the firmware must answer with the expected new device->host catalogue entries (count and frame-count deltas) |
| `sleep` | `seconds` 0.1..10 | bounded wait |
| `note` | `about` | documentation only |

Expectations on a read: exactly one comparison of `eq`, `ne`, `zero`,
`nonzero` or `ge`, with an optional `mask` (default full word).  A read without
`expect` is *captured* (evidence only, no gate).  Example gates from the
bundled ladder:

    {"label": "page8", "bar0": "0x877008", "expect": {"eq": "0x2"}}   # +8==2 latch
    {"label": "ctx190p", "bar0": "0x7d45e8", "expect": {"nonzero": true}}
    {"label": "ctx0b", "bar0": "0x7d4458",
     "expect": {"mask": "0x000000ff", "eq": "0x00000004"}}            # ctx[0]=4

## 4. Dynamic reads and pointer conventions

The firmware's allocator returns **device CAs**, not firmware-CPU pointers: in
the hardware-validated type-1 lap `ctx+0x190` read `0x01011448`, a PKTRAM
region-2 CA (not a fw-RAM CPU address; the `V + 0x6B8000` formula in the old
C11 read table is only valid for CPU pointers).  A read may therefore use
`{"base": "<earlier label>", "add": "<hex>"}` and the runner resolves the pointer
through the real endpoint inbound regions at runtime:

| convention | pointer range | BAR0 |
| --- | --- | --- |
| `pktram` | `0x01000000..0x011dffff` | `0x01d8000 + (ca - 0x01000000)` |
| `acp` | `0x02000000..0x021dffff` | `0x04d8000 + (ca - 0x02000000)` |
| `fwram-ca` | `0x01200000..0x0137ffff` | `0x06b8000 + (ca - 0x01200000)` |
| `io` | `0x40000000..0x4011ffff` | `0x03b8000 + (ca - 0x40000000)` |
| `tcm` | `0x00400000..0x00417fff` | `0x01c0000 + (ca - 0x00400000)` |
| `cpu` | CPU view `0x0..0x21ffff` | `0x06b8000 + cpu` |

The generated script emits the resolver inline (`PTR-CONV <check> base=... conv=...`),
stops with `<step>.bad-pointer` if the pointer matches no convention, and the
host evaluator records `conv` / `resolved_bar0` in the evidence JSON.  The
bundled post-create buffer reads are deliberately **captured-only**: the
successful lap did not measure those buffers through this mapping yet (the
gate is on `ctx+0x190`, `ctx[0]=4`, `ctx+0x434`, the table entry, mask and
staging, exactly what the task lane requires).

## 5. The bundled sequence: `ladder-c-ordered`

| step | kind | what it gates |
| --- | --- | --- |
| `boot-golden` | read_pack | golden page `0x86807fef/7/2/0xabcd`, state 1, mask 0, staging 0 (+ cave capture) |
| `catalog-boot` | catalog_snapshot | D2H catalogue baseline before the H2D pair |
| `pre-create-pack` | read_pack | G/table entry, `ctx+0x190`=0, `ctx+0x434`=0, refcount 0, pool-1 usable, mask/staging 0 |
| `preflight-plan` | attr_expect | `h2d_plan` shows both canned frames (48 B wire, node 48/72) and the ctx_create gate |
| `ctx-create-t1` | attr_write | ONE `h2d_ctx_create=1`; `h2d_status` shows frames=1/ctx_sent=1/ctx_created=1/ACTIVE/msg=ctx_create_t1 |
| `post-create-pack` | read_pack | `ctx+0x190` nonzero, `ctx[0]=4`, `ctx+0x434` nonzero, table entry unchanged, mask 0, staging 0 (buffer reads captured) |
| `pre-txq-status` | attr_expect | frames=1, txq=0, ctx_created=1, `txq_sync72=ok`, mbox_inflight 0 |
| `txq-sync72` | attr_write | ONE `h2d_txq_sync72=1`; frames=2/txq=1/ok=2/timeout=0/msg=txq_sync72 |
| `post-txq-pack` | read_pack | **mask 0 + staging 0** (the whole point), out0/out2 drained, context alive, page/state golden |
| `d2h-responses` | d2h_catalog | NEW `g=1 id=0x12` (40 B) and `g=1 id=0x03` (36 B) entries, frame-count delta vs. the boot snapshot |
| `done-note` | note | stop after the ordered lap |

The two H2D writes are the only message triggers; everything else is
read-only.  One frame per idea, one ordered pair per boot, no retry, no
`rmmod` after release, never CA 0x400392F0, never 0xCECE during boot.

## 6. Evidence record

A real run writes (into the lane's gitignored evidence dir, normally
`build/wifi-bench/<session-id>/`):

| file | content |
| --- | --- |
| `protocol-plan.json` | the validated sequence plan (steps, checks, writes, pins) |
| `protocol-evidence.json` | `hi5622-protocol-evidence/1`: per-step/per-check host verdict + captured actual + device gate verdict, boot/mailbox observables, rd32 map, session/module/firmware hashes, totals |
| `PROTOCOL-SEQUENCE.md` | human-readable step table, every check with actual value, the boot observables and the failure triage block |
| `session-plan.json` / `device-script.sh` | the full session plan and the exact device-side script |
| `report.txt` / `summary.json` | raw report and parsed summary (the protocol evidence is also embedded) |
| `writes-host.jsonl` / `writes-device.log` | sha256-chained planned writes + the device's write log |
| `preflight.txt` | host gate results |

Evidence semantics: each check carries a **host** verdict recomputed from the
captured report text and the **device** gate verdict that actually stopped the
script.  A device PASS with a host FAIL (or vice versa) is a disagreement and
is surfaced, never hidden.  Stop-on-first-failure: after the first FAIL every
later step is `NOT-RUN`, so one boot yields exactly one failing step.

## 7. Failure triage table

The evidence record prints these codes next to the failed check; the same
text is in `PROTOCOL-SEQUENCE.md` under "Failure triage".

| code | meaning | host-only next step |
| --- | --- | --- |
| `read-mismatch` | boot/PACK word differs from the expected PD4 golden state | do not fire another message; re-read boot/hcc_ready/rd_status, capture raw values, treat as a different experiment |
| `read-missing` | a read label was never emitted | find the first failing step from `stop_rule`; do not rerun blindly |
| `attr-pattern-missing` | a sysfs gate string did not match | capture h2d_status/h2d_plan/hcc_events/dmesg; compare with the sequence step text; never raise the budget mid-session |
| `attr-capture-missing` | the post-write capture is absent (store failed) | do not resend; power-cycle for another boot |
| `write-failed` | the H2D store failed (announce not consumed / gate refused) | capture out[0]/out[2], hcc_rings SR0 producer/consumer, page/mask; no retry in this session |
| `catalog-entry-missing` | the firmware did not answer with the expected D2H frame | consumed + new responses is the known working lap; absence is a discovery: capture rd_catalog/rd_decode and stop |
| `catalog-frames-not-increased` | no new D2H frame count | capture rd_catalog/rd_decode/hcc_events vs. the before snapshot |
| `resolved-pointer-outside-window` | a dynamic pointer matched no inbound region (or resolved outside BAR0) | report the raw base value; do not guess the mapping |
| `device-gate-fail` | the device-side gate failed while the host evidence looked different | capture the evidence dir; follow the check's own triage |
| `step-not-run` | skipped because an earlier step stopped the session | fix/report the first failing step; do not resume mid-boot |

Device-side stops that never reach a protocol step keep the existing mode
stop rules (`attr-write-failed`, `ready-id1-timeout`, `ready-id2-timeout`,
`module-sha-mismatch`, `firmware-sha-mismatch`, `seed ...`) and are recorded in the
same evidence record.

## 8. Adding future messages without code edits

The proto-seq / datapath lanes can add the next vendor startup message as a
JSON change only:

1. copy the sequence (or add a step) and pick the driver attribute that emits
   the message (existing: `h2d_ctx_create`, `h2d_txq_sync`, `h2d_txq_sync72`, `h2d_send`);
2. for a new message add an `attr_write` step (`attr`, `value`) plus the
   `h2d_status` patterns that prove it was committed/consumed, a `read_pack` for
   its PACK state, and a `d2h_catalog` step for the expected response;
3. `python3 tools/bench_session.py --list-sequences` validates it (unknown fields,
   bad attributes and unpinned firmware are refused before anything is
   deployed); the dry-run then shows the full gate table.

If the message is a brand-new driver attribute it is not yet in the safety
census; add it to `bench_guards.WO_ATTRS` (and `H2D_ATTRS` if it is an H2D
trigger) after the driver review - that is the only code change a new message
needs, and the static script audit then covers it automatically.  The
protocol profile also refuses any module other than the sequence's
`module_names` and any firmware whose sha256/length differs from the pinned
PD4 image, so a future experiment cannot silently boot a different image.

## 9. Tests and limits

- `./tools/run-selftest.sh`: **258/258** device-free checks, including +81
  new protocol checks: schema/strictness refusals, pointer-convention
  resolution, plan/audit/`sh -n` for the protocol mode, the generated `gate_num`
  shell function executed under `/bin/sh` (full-mask string compare, low/high
  byte masks, 16-bit ge), synthetic end-to-end `execute()` against a fake
  transport (PASS + read-gate FAIL + attr-pattern FAIL fixtures, NOT-RUN
  accounting, evidence artifacts, triage rendering), and the CLI
  (`--list-sequences`, dry-run, budget/parameter refusals).
- `tools/fw_patch_builder.py --selftest`: 27/27 (unchanged; PD4 builds to the
  pinned `cfe24bfb...` locally).
- Limits: the protocol runner executes the device script as one POSIX shell
  script, so it cannot adapt mid-run (by design - stop-on-first-failure);
  a new idea needs a fresh boot.  Dynamic-read buffer expectations are
  captured-only until the PKTRAM buffer contents are measured on hardware.
  The lane never contacts the device and does not claim the automation has
  been fired; the bundled gates mirror the 2026-11-25 hardware-validated lap.
