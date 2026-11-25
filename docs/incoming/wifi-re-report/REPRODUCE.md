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
claim-ID sequence, table/finding completeness, the v2-v5 milestone vocabulary, key-constant
cross-checks against the owning lanes' committed sources and the superproject ledger,
forbidden-content scan) and a final PASS with the check count. v5 added the static-RE-wave checks
(sections 6.1-6.17, findings F1-F19, tables T1-T37, claims EV-001..EV-305); **v6 extends the same
checks to sections 6.19-6.25, findings F21-F27, tables T39-T45 and claims EV-309..EV-340** - the
association module identities and 56 params, the corrected g0 id6 mgmt-TX carrier, the g1 id13
management-RX event, the SET_RX_FILTER negative, the RX arm ordering (0x182 vs 0x02), the R11
`field10` values, the RF path-loss observation and the 51/51 public-source manifest. At the v6
cut-off the verifier reports **RESULT: PASS (290/290 checks)**. Exit code 0 on success.

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
$PY wifi-bench-tooling/tools/selftest.py            # 503/503, PASS (incl. protocol mode, phase matrix, blocked outcomes)
cd $REPO/opensource/docs/incoming/wifi-vap-scan && ./tools/build.sh   # 137/137 PASS, byte-exact g0 id7/id8/id9/id0xC builders
cd $REPO/opensource/docs/incoming/wifi-scan-ap && $PY tools/selftest.py   # 294/294 PASS, provider caps 0x1f + fail-closed defaults
cd $REPO/opensource/docs/incoming/wifi-vap-start && ./tools/build.sh      # 61/61 PASS, mode-1 VAP add/start builders + WAL envelopes
cd $REPO/opensource/docs/incoming/wifi-scan-complete && ./tools/build.sh  # 373/373 + clean 6.18.60 module build
cd $REPO/opensource/docs/incoming/wifi-ap-start && ./tools/build.sh       # 128/128 AP-start frame builders + emit
cd $REPO/opensource/docs/incoming/wifi-ap-start && $PY tools/ap_start_re.py --selftest   # 63/63 static probes
cd $REPO/opensource/docs/incoming/wifi-hostapd-deploy && ./tools/selftest.sh   # 14/14 host-only; then ./deploy-hostapd.sh --check (88/88) and ./tools/check-ladder.py (107/107)
cd $REPO/opensource/docs/incoming/wifi-dp-air && ./tools/build.sh          # merge provenance + 1,483 checks + F-AIR sequence + clean 6.18.60 module build
cd $REPO/opensource/docs/incoming/wifi-rx-static && ./tools/build.sh     # 51/51 host checks (live bank + header) + clean 6.18.60 build
cd $REPO/opensource/docs/incoming/wifi-wpa-static && ./tools/run-host-check.sh   # key selftest (cipher classes, 3 bodies/frames, refusals)
cd $REPO/opensource/docs/incoming/wifi-scan-full && ./tools/build.sh      # 90/90 host + 65/65 static-evidence + clean 6.18.60 build
cd $REPO/opensource/docs/incoming/wifi-beacon-static && ./tools/build-host.sh   # 65/65 host checks (five-message plan/channel KAT)
cd $REPO/opensource/docs/incoming/wifi-beacon-static && ./tools/build-module.sh # 33/33 static probes x2 vendor versions + 6.18.60 build
cd $REPO/opensource/docs/incoming/wifi-sta-static && ./tools/build-sta-static.sh # host tests + 6.18.60 build (WAL connect/5 GHz/HT-VHT)
cd $REPO/opensource/docs/incoming/wifi-scan-tuning && ./tools/build.sh    # scan batching/timing host checks + clean module build
cd $REPO/opensource/docs/incoming/wifi-datapath-bringup && bash tools/build.sh  # 69/69 + 308/308 + 34/34, 6.18.60 module build (host-only; no device)
$PY wifi-datapath/tools/build.sh                     # 77/77 PASS, frame builders + ring arithmetic
$PY wifi-datapath/tools/fw_dp_tables.py --selftest   # 35/35 PASS, firmware key tables A/B
bash wifi-proto-seq/tools/selftest.sh                # 66/66 + 38/38 + 55/55 PASS, calibration family
$PY wifi-ete-wake/tools/ete_wake.py selftest         # 81/81 PASS, ETE trigger chain
```

Frame cross-checks: `wifi-h2d-frames/tools/build.sh` runs the catalogue `--check` and the
byte-exact builder vectors, including the 48-byte TXQ config sync quoted in [TABLES.md](TABLES.md)
T13 (F3); `wifi-h2d-send/tools/build.sh` additionally emits the 72-byte-node variant and the
type-variant context-create frames (kernel build required for the module part).
The v2 calibration frames are reproduced by `wifi-proto-seq/tools/proto_seq_frames.py`
(`--emit 3507` prints the 298-byte structural frame; `--check-response` validates an answer hex).

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

## 4.1 Reproduce the v6 association / management wave (host-only)

```sh
cd $REPO/opensource/docs/incoming
# corrected device management-TX carrier + sibling-derived RX model (sibling-GPL lane)
gcc -std=c99 -Wall -Wextra -O2 -I wifi-sibling-gpl/src -o /tmp/mgmt_ctrl_selftest \
    wifi-sibling-gpl/tools/mgmt_ctrl_selftest.c wifi-sibling-gpl/src/hi5622_mgmt_logic.c
/tmp/mgmt_ctrl_selftest                             # 41/41
gcc -std=c99 -Wall -Wextra -O2 -I wifi-sibling-gpl/src -o /tmp/rx_model_selftest \
    wifi-sibling-gpl/tools/rx_model_selftest.c
/tmp/rx_model_selftest                              # 94/94
# the carrier patch applies to the final association tree (dry-run; tree = build-script output)
cd /tmp/wifi-assoc-build/integ 2>/dev/null && patch -p1 --dry-run \
    < "$REPO/opensource/docs/incoming/wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch"
# the final association module (host build; needs the 6.18.60 KDIR + OpenWrt toolchain; .ko local)
cd $REPO/opensource/docs/incoming/wifi-stability && ./tools/build-association-module.sh
```

Identity checks: the stability lane records the final association module as **56 params, stripped
344,472 B sha256 `251d08f0...`**, and the corrected-carrier rebuild as **344,372 B sha256
`ab9e18a4...`** (`wifi-sibling-gpl/evidence/mgmt-carrier-artifact.txt`); the wire/layout facts are
in [TABLES.md](TABLES.md) T39-T44. The public-source manifest is in
`wifi-public-research-2/SOURCES.tsv` + `VERIFY-fetch.txt` (51/51 byte+sha matches at the pinned
commits; re-fetching is a network action). The R11 scan ladder is `wifi-scan-full/TEST-LADDER-R11.md`
(attended; the final sweep values are in T44), and the RF statement (~44 dB path loss) is a bench
observation, not a reproducible host test.

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
# the independent audit of PD4 (identity, delta, cave census, H1/H2, variants):
# NOTE: the H1/H2 read-only pack itself was a bench (device-attended) read; its result is quoted
# from the ledger 2026-11-25 21:30 and integrated in REPORT 6.6/F10 and TABLES T25. The host tools
# re-verify the static prediction only.
$PY wifi-fw-audit/tools/verify_pd4.py --selftest              # 14/14 PASS (no vendor blob needed)
$PY wifi-fw-audit/tools/verify_pd4.py                         # 31 checks; reads the local pinned blob
$PY wifi-fw-audit/tools/pd4_variants.py --selftest            # 14/14 PASS
```

The patched images exist only outside the repository. Do not copy them into it.

## 6. Re-run the bench result (attended only)

Read first: `wifi-driver-clean/BENCH-PROCEDURE.md`, `wifi-h2d-send/BENCH-PROCEDURE.md` Ladder C
(ordered context-create -> TXQ) and Ladder D (calibration C1), `wifi-bench-tooling/README.md`,
`wifi-cfg80211/REPORT.md` sec 8 (interface observation session) and the project's `RUNBOOK.md`.
The generic ladder with pass gates is [TABLES.md](TABLES.md) T19. The cfg80211 bench-image build
and extract steps are in `wifi-upstream-target/CFG80211-BENCH-IMAGE.md` sec 2-4 (bench-only); the
ABI-correct image and the attended flash procedure are in `wifi-abi-image/REPORT.md` +
`wifi-abi-image/FLASH-RUNBOOK.md` (the flash itself was executed 2026-11-27 06:30: kernel slot
read-back `1794a531...`, `ubiformat` OK, post-boot ABI receipt = the kmod pin; see REPORT 6.11/C44).
One-command session shape:

```sh
cd $REPO/opensource/docs/incoming/wifi-bench-tooling
$PY tools/bench_session.py --list
$PY tools/bench_session.py --mode patched --dry-run \
    --module /path/hi5622_wifi.ko \
    --plat-block /path/plat_block.txt --wifi-block /path/wifi_block.txt
# only in an attended session, with the recovery door armed:
#   $PY tools/bench_session.py --mode patched ...   (follow the printed plan step by step)
```

The v3 bring-up matrix (REPORT 6.8, TABLES T28) is the operator entry point for the whole state
table; it is dry-run by default and needs the attended/recovery gates for a real run:

```sh
cd $REPO/opensource/docs/incoming/wifi-bench-tooling
$PY tools/phase_matrix.py --list-phases
$PY tools/phase_matrix.py --phase all --dry-run \
    --module ../wifi-h2d-send/src/hi5622_wifi.ko \
    --module-for D1=../wifi-cfg80211/src/hi5622_wifi.ko \
    --module-for D2=<the mode-1 vap-start provider module, e.g. a 738,112 B e09dc697 build> \
    --module-for E1=../wifi-datapath-bringup/src/hi5622_wifi.ko \
    --module-param-for D2 'allow_frames=12 vap_start_first=1 vap_start_mode=1 vap_start_module=0 frame_gap_ms=100 h2d_send_budget=8' \
    --sequence-for D2 <the D2 TRUE-PASS ladder> \
    --fw-file /tmp/hi5622-patched/FIRMWARE.PD4.bin \
    --fw-manifest /tmp/hi5622-patched/manifest.json \
    --wifi-block-bin /tmp/blocks/wifi_block.bin \
    --cfg80211-ko /path/cfg80211.ko --iw-bin /path/iw --libnl-tiny /path/libnl-tiny.so
# attended only, recovery door armed:
#   $PY tools/phase_matrix.py --phase all --reboot-between --yes-really --attended ...
```

The real-frame session (REPORT 6.7, TABLES T27) uses the cfg80211 mode with the scan-ap provider
module (676,180 B sha256 `4a1c3b9a...`, caps 0x1f) and a small `allow_frames` budget (`N`) so the
VAP `user_add`/`user_add_notify` and the 520 B scan request are posted; it must run after the C2
ladder in the same boot and is attended-only.

The v4 ladders extend that flow, all attended and fail-closed:

* **scan results (REPORT 6.9)** - load the mode-1 provider (`vap_start_first=1 vap_start_mode=1
  vap_start_module=0`), create the VAP as managed, run one scan; expect the mode-1 VAP base
  (`g0 id1` 48 B + 40 B) and then `g1 id17`/`g1 id15` results. The D2 TRUE-PASS ladder is the
  scripted form; `scan-complete/TEST-LADDER.md` has the full-coverage rungs (auto window /
  `scan_timeout_ms`).
* **AP start (REPORT 6.10/6.11, TABLES T29/T30)** - deploy hostapd-mini + ucode via
  `wifi-hostapd-deploy/deploy-hostapd.sh --apply --attended --yes-really`; load the v7 AP-start
  module (`fd8e1439...`) with `vap_start_mode=1`; `iw phy phy0 interface add wlan0 type __ap`;
  one bounded scan for the VAP base; `hostapd -B ... hostapd-open.conf`; then the read-only readback
  pack (T29) - `AP-ENABLED` alone is not the gate.
* **air-TX (REPORT 6.12, TABLES T31)** - deploy the merged data-plane module (`ca32aec4...`), run
  the F-AIR sequence with `cat dp_probe` (READ) -> `echo 1 > dp_setup` -> `echo 1 >
  h2d_txq_addr_sync` -> one `dp_tx_frame` -> read-only `dp_rx_poll`; POST-ONLY is a recorded
  outcome, not a failure; an on-air claim needs an external observer.
* **static-wave feature ladders (REPORT 6.13-6.17, TABLES T32-T37)** - each lane ship a fail-closed
  parameter and an attended ladder; the fired rungs were: `rx_enable=1` + `cat dp_rx_arm` (live-bank
  proof), `wpa_keys=1` + WPA2 hostapd (GTK install), `beacon_static=1 beacon_vap_start=1` (five-message
  vendor order), and the dp-tuning `tx_complete_ms` ladder (TX COMPLETED). All are attended-only and
  must run on a fresh PD4 golden boot; never re-send a refused frame in the same boot.
* **v6 association/management ladders (REPORT 6.19-6.25, TABLES T39-T45)** - load the corrected
  association module (`ab9e18a4...`; rebuild via the stability build script + the sibling carrier
  patch) with `mgmt_tx=1 mgmt_rx=1 sta_ops=1 eapol_rx=1 data_tx=1 rx_enable=1 beacon_static=1
  beacon_vap_start=1 wpa_keys=1`; fire the five-message AP order, then (**AP first**) `cat dp_probe`
  -> `echo 1 > dp_setup` -> `echo 1 > h2d_txq_addr_sync` -> `echo 1 > dp_rx_arm` -> hostapd WPA2
  AP -> client L5 (auth/assoc via `g1 id13` + the corrected `g0 id6` carrier, then EAPOL through
  `ndo_start_xmit`/`dp_rx_drain`). Expect one `g0 id6` management frame posted/consumed (86 B),
  `g1 id13` management RX on the CRX path and the live-bank 0x182 readback. **RF precondition:**
  antennas attached - with none the AP TX is ~-64 dBm at 30 cm and the client cannot associate
  (6.25). One load per boot; no retry.
* **image (REPORT 6.11/C44)** - the ABI-correct image and the flash runbook are in
  `wifi-abi-image/`; the flash was executed on 2026-11-27 06:30 and the post-boot ABI receipt is the
  acceptance check (`apk query --fields version kernel` = the kmod pin).

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
