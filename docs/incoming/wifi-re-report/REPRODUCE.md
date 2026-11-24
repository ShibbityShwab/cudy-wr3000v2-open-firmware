# REPRODUCE - re-deriving and re-running the report's evidence

Two separate reproduction paths: **static** (host-only, no device; re-derives every protocol claim
from the committed tools and the local vendor artifacts) and **bench** (attended, recovery-gated;
re-runs the operational loop). This file is a map, not an authorisation: the bench path requires
the project's physical rules (`BENCH-FACTS.md`, `RUNBOOK.md`, recovery door armed, human
attending).

Environment used throughout:

```sh
REPO=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
PY=$REPO/.provision-venv/bin/python          # project venv; stock python3 lacks capstone etc.
cd $REPO/opensource/docs/incoming
```

Local vendor artifacts (never committed) referenced by the tools:

| artifact | local path |
| --- | --- |
| vendor firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...`) |
| mask ROM dump | `/home/shibbityshwab/staging-fi/rom16k.bin` (16,384 B, sha256 `fcfd464f...`) |
| vendor kernel modules | `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/` |
| stock 2.5.24 rootfs | local extraction (the vendor-userspace lane's `tools/regen-evidence.sh` takes a path) |

The tools read these **in place** and print derived annotations; none copies vendor bytes into the
repository.

---

## 1. Verify this report first (no vendor blob needed)

```sh
cd $REPO/opensource/docs/incoming/wifi-re-report
$PY tools/verify_report.py
```

Expected: a list of PASS lines (file presence, lane-path resolution, marker set, section coverage,
claim-ID sequence, key-constant cross-checks against the owning lanes' committed sources, forbidden
content scan) and a final PASS with the check count. Exit code 0 on success.

## 2. Reproduce the boot-chain facts

```sh
cd $REPO/opensource/docs/incoming
$PY wifi-rom-dump/tools/romdis.py selftest          # 86/86 - ROM map, S0/S1, gate writers
$PY wifi-rom-dump/tools/romdis.py map               # the ROM range table (TABLES T3)
$PY wifi-rom-dump/tools/romdis.py gate              # the dual-use 0x4000010C analysis
$PY wifi-rom-s01/tools/rom_s01.py selftest          # 70/70 - handshake page contract
$PY wifi-rom-s01/tools/rom_s01.py handshake         # page writers + observed signatures
$PY wifi-s1-cpu1/tools/s1_cpu1.py selftest          # 46/46 - TCM handoff, S1 identity
$PY wifi-s1-cpu1/tools/s1_cpu1.py tcm               # platform-block contract, TCM blobs
$PY wifi-chip-reset/tools/chip_power.py selftest    # 13/13 - vendor reset pulse timing
$PY wifi-rom-flow/tools/fwdis.py info               # image layout / address conventions
```

## 3. Reproduce the protocol core and frame builders

```sh
cd $REPO/opensource/docs/incoming
wifi-hcc-core/tools/build.sh --no-kernel            # host selftest 528 checks, PASS
wifi-h2d-frames/tools/build.sh                      # host build -Werror + 233 checks, PASS
$PY wifi-d2h-direct/tools/d2h_selftest.py           # 92/92, PASS
$PY wifi-ctx-create/tools/ctx_create_re.py --selftest    # 40/40
$PY wifi-ctx-create/tools/ctx_create_frame.py --selftest # 26/26
$PY wifi-ctx-create/tools/ctx_create_re.py --trace       # handler -> key table -> create helper
$PY wifi-ctx-create/tools/ctx_create_frame.py --frame    # the 48-byte create frame (TABLES T13 F4)
$PY wifi-bench-tooling/tools/selftest.py            # 177/177, PASS
```

Frame cross-checks: `wifi-h2d-frames/tools/build.sh` runs the catalogue `--check` and the
byte-exact builder vectors, including the 48-byte TXQ config sync quoted in [TABLES.md](TABLES.md)
T13 (F3); `wifi-h2d-send/tools/build.sh` additionally emits the 72-byte-node variant and the
type-variant context-create frames (kernel build required for the module part).

## 4. Reproduce the static protocol tables

```sh
cd $REPO/opensource/docs/incoming
$PY wifi-h2d-catalog/tools/fw_h2d_tables.py --verify   # 20 byte-level claims on the H2D tables
$PY wifi-h2d-catalog/tools/h2d_join_vendor.py          # vendor sender <-> firmware slot coverage
$PY wifi-h2d-catalog/tools/payload_offsets.py          # probe-safety classes (TABLES T10)
$PY wifi-ete-engine/tools/ete_engine.py selftest       # 150/150 - ETE window/writers/negative
$PY wifi-ete-engine/tools/ete_engine.py layout         # ETE register window map (TABLES T14)
$PY wifi-fw-postboot/tools/regmap_probe.py             # the instrument-aliasing correction (C7)
$PY wifi-fw-postboot/tools/fwdis.py dis 0xC2EFC 0xA0   # event dispatcher
$PY wifi-h2d-send/tools/fw_g0id1_handler.py            # g0 id1 handler analysis (38/38)
$PY wifi-h2d-send/tools/ctx_type_dispatch.py           # create-helper type dispatch (26/26)
```

Vendor-side interface facts (host-driver reversal; needs the local vendor modules):

```sh
$PY wifi-driver-re/...                                 # see wifi-driver-re/REPORT.md for its tools
$PY wifi-host-replica/tools/...                        # relocation-aware disassembly/call census
$PY wifi-vendor-userspace/tools/regen-evidence.sh /path/to/extracted/rootfs
```

## 5. Reproduce the firmware patch recipes (host-only)

```sh
cd $REPO/opensource/docs/incoming
$PY wifi-irq-unmask/tools/fw_pd_builder.py --list
$PY wifi-irq-unmask/tools/fw_pd_builder.py --build PD3        # writes to /tmp/hi5622-patched
$PY wifi-irq-unmask/tools/fw_pd_builder.py --recipe PD3
$PY wifi-irq-unmask/tools/pd2_re.py selftest                  # 33/33
$PY wifi-irq-unmask/tools/pd3_re.py selftest                  # 15/15
$PY wifi-irq-unmask/tools/pd4_re.py cave                     # the 0x108240 cave proof
$PY wifi-bench-tooling/tools/fw_patch_builder.py --verify-manifest /tmp/hi5622-patched/manifest.json
```

The patched images exist only outside the repository. Do not copy them into it.

## 6. Re-run the bench result (attended only)

Read first: `wifi-driver-clean/BENCH-PROCEDURE.md`, `wifi-h2d-send/BENCH-PROCEDURE.md` Ladder C,
`wifi-bench-tooling/README.md`, and the project's `RUNBOOK.md`. The generic ladder with pass gates
is [TABLES.md](TABLES.md) T19. One-command session shape:

```sh
cd $REPO/opensource/docs/incoming/wifi-bench-tooling
$PY tools/bench_session.py --list
$PY tools/bench_session.py --mode patched --dry-run \
    --module /path/hi5622_wifi.ko \
    --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt
# only in an attended session, with the recovery door armed:
#   $PY tools/bench_session.py --mode patched ...   (follow the printed plan step by step)
```

Mandatory rules (from the project, restated because violating them has cost recovery cycles):

* UART is read-only; never send host->router bytes; never open the tty read-write.
* One idea per fire; stop on any unexpected exception mask or page change; never retry a frame.
* Release-only boot: `0x00005A5A` to CA `0x40000108`; never write `0xCECE` during boot.
* Never write CA `0x400392F0`; never write `0xCECE` to the gate except evidence-driven post-latch.
* Never flash without `tools/fire-preflight.sh` and a named delivery plan in `STATUS.md`.
* The box is never left without a recovery path; the bench is never touched by an unauthorised agent.

## 7. Housekeeping checks before committing anything

```sh
cd $REPO
tools/check-repo.sh                       # project repo policy check
opensource/tools/publish-prep-check.sh    # publication content check (vendor bytes/secrets)
git -C opensource status --short          # commit only your lane's paths
```

The `opensource/` submodule is the only place this lane commits; no vendor binary, firmware byte,
ROM dump, calibration value or secret may appear in a commit.
