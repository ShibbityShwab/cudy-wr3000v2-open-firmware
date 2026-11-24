#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# run-host-suite.sh - host-only verifier suite for the wifi-closeout lane.
#
# Runs every lane selftest/verifier that needs no device, no network and no
# write outside the repo's gitignored trees.  A snapshot of the project kit:
#   - concurrent agents' lane dirs are read-only from here (we never edit);
#   - a lane verifier that legitimately fails because it is mid-edit by its
#     owner is reported as-is (the close-out TEST-MATRIX.md classifies every
#     non-PASS entry).
#
# Usage:
#   ./tools/run-host-suite.sh                 # everything (~4-5 min)
#   ./tools/run-host-suite.sh --list          # print entries
#   ./tools/run-host-suite.sh --only LANE     # substring match on entry name
#   ./tools/run-host-suite.sh --fast          # skip kernel-module builds
#   ./tools/run-host-suite.sh --external      # also run the tarball-based release-line validator
#
# Exit codes per entry: 0 PASS; 77 SKIP (missing local prerequisite); 124
# timeout; anything else FAIL.
#
# Output:
#   build-evidence/host-suite/<name>.log      full stdout+stderr per entry
#   build-evidence/host-suite/summary.tsv     name, rc, seconds, pass-count line
#   build-evidence/host-suite/RESULT.txt      PASS/FAIL/SKIP table + totals
#
# Exit: 0 if every non-SKIP entry passed, 1 otherwise.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$LANE/../../../.." && pwd)
INC="$ROOT/opensource/docs/incoming"
OUT="$LANE/build-evidence/host-suite"
PY="$ROOT/.provision-venv/bin/python"
[ -x "$PY" ] || PY=python3
FAST=0
EXT=0
ONLY=""
MODE="run"
while [ $# -gt 0 ]; do
  case "$1" in
    --list) MODE="list" ;;
    --fast) FAST=1 ;;
    --external) EXT=1 ;;
    --only) shift; ONLY=$1 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done
mkdir -p "$OUT"
SUMMARY="$OUT/summary.tsv"
: > "$SUMMARY"

# name | dir | timeout | command | class(fast|kernel|external)
ENTRIES=""
add() { ENTRIES="${ENTRIES}$1|$2|$3|$4|$5
"; }
add "wifi-bench-tooling.selftest"    "$INC/wifi-bench-tooling" 180 "./tools/run-selftest.sh" fast
add "wifi-bench-session.selftest"    "$INC/wifi-bench-session" 120 "./tools/selftest.sh" fast
add "wifi-bench-session.artifacts"   "$INC/wifi-bench-session" 120 "./tools/check-artifacts.sh" fast
add "wifi-bench-tooling.fw-builder"  "$INC/wifi-bench-tooling" 180 "$PY tools/fw_patch_builder.py --selftest" fast
add "wifi-fw-audit.pd4"              "$INC/wifi-fw-audit" 120 "$PY tools/verify_pd4.py --selftest" fast
add "wifi-fw-audit.variants"         "$INC/wifi-fw-audit" 120 "$PY tools/pd4_variants.py --selftest" fast
add "wifi-ete-engine.selftest"       "$INC/wifi-ete-engine/tools" 180 "$PY ete_engine.py selftest" fast
add "wifi-ete-wake.selftest"         "$INC/wifi-ete-wake/tools" 180 "$PY ete_wake.py selftest" fast
add "wifi-rom-s01.selftest"          "$INC/wifi-rom-s01" 180 "$PY tools/rom_s01.py selftest" fast
add "wifi-s1-cpu1.selftest"          "$INC/wifi-s1-cpu1" 180 "$PY tools/s1_cpu1.py selftest" fast
add "wifi-rom-dump.selftest"         "$INC/wifi-rom-dump" 180 "$PY tools/romdis.py selftest" fast
add "wifi-chip-reset.selftest"       "$INC/wifi-chip-reset/tools" 120 "$PY chip_power.py selftest" fast
add "wifi-hcc-service.selftest"      "$INC/wifi-hcc-service" 120 "$PY tools/frame_selftest.py" fast
add "wifi-d2h-direct.selftest"       "$INC/wifi-d2h-direct" 120 "$PY tools/d2h_selftest.py" fast
add "wifi-ete-frames.verify"         "$INC/wifi-ete-frames" 120 "$PY tools/verify-frames.py" fast
add "wifi-datapath.tables"           "$INC/wifi-datapath" 120 "$PY tools/fw_dp_tables.py --selftest" fast
add "wifi-datapath.build"            "$INC/wifi-datapath" 180 "./tools/build.sh" fast
add "wifi-h2d-frames.build"          "$INC/wifi-h2d-frames" 180 "./tools/build.sh" fast
add "wifi-h2d-inject.selftest"       "$INC/wifi-h2d-inject" 180 "./tools/build.sh --selftest-only" fast
add "wifi-hcc-core.host"             "$INC/wifi-hcc-core" 180 "./tools/build.sh --no-kernel" fast
add "wifi-vap-scan.build"            "$INC/wifi-vap-scan" 180 "./tools/build.sh" fast
add "wifi-vap-start.build"           "$INC/wifi-vap-start" 180 "./tools/build.sh" fast
add "wifi-ap-start.build"            "$INC/wifi-ap-start" 180 "./tools/build.sh" fast
add "wifi-ap-start.re"               "$INC/wifi-ap-start" 120 "$PY tools/ap_start_re.py --selftest" fast
add "wifi-scan-d2h.selftest"         "$INC/wifi-scan-d2h" 180 "./tools/selftest.sh" fast
add "wifi-proto-seq.selftest"        "$INC/wifi-proto-seq" 120 "./tools/selftest.sh" fast
add "wifi-openwrt-pkg.selftest"      "$INC/wifi-openwrt-pkg" 120 "./tools/selftest.sh" fast
add "wifi-hostapd-deploy.check"      "$INC/wifi-hostapd-deploy" 180 "./deploy-hostapd.sh --check --evidence /tmp/wifi-closeout-hostapd-check.txt" fast
add "wifi-hostapd-deploy.selftest"   "$INC/wifi-hostapd-deploy" 120 "./tools/selftest.sh --evidence /tmp/wifi-closeout-hostapd-selftest.txt" fast
add "wifi-hostapd-deploy.ladder"     "$INC/wifi-hostapd-deploy" 120 "./tools/check-ladder.py" fast
add "wifi-image-deploy.selftest"     "$INC/wifi-image-deploy" 120 "./tools/selftest.sh" fast
add "wifi-image-integration.selftest" "$INC/wifi-image-integration" 120 "./tools/selftest.sh" fast
add "wifi-abi-image.selftest"        "$INC/wifi-abi-image" 120 "./tools/selftest.sh" fast
add "wifi-re-report.verify"          "$INC/wifi-re-report" 180 "python3 tools/verify_report.py" fast
add "wifi-driver-clean.build"        "$INC/wifi-driver-clean" 240 "./tools/build.sh" kernel
add "wifi-h2d-send.build"            "$INC/wifi-h2d-send" 240 "./tools/build.sh" kernel
add "wifi-cfg80211.build"            "$INC/wifi-cfg80211" 240 "./tools/build.sh" kernel
add "wifi-scan-ap.build"             "$INC/wifi-scan-ap" 240 "./tools/build.sh" kernel
add "wifi-scan-complete.build"       "$INC/wifi-scan-complete" 240 "./tools/build.sh" kernel
add "wifi-datapath-bringup.build"    "$INC/wifi-datapath-bringup" 300 "./tools/build.sh" kernel
add "wifi-dp-txrx.build"             "$INC/wifi-dp-txrx" 300 "./tools/build.sh" kernel
add "wifi-dp-air.build"              "$INC/wifi-dp-air" 360 "./tools/build.sh" kernel
add "wifi-hcc-core.build"            "$INC/wifi-hcc-core" 300 "./tools/build.sh" kernel
add "wifi-vap-start.kernel"          "$INC/wifi-vap-start" 300 "./tools/kernel-check.sh" kernel
add "wifi-h2d-frames.kernel"         "$INC/wifi-h2d-frames" 300 "./tools/kernel-check.sh" kernel
add "wifi-ap-start.kernel"           "$INC/wifi-ap-start" 300 "./tools/kernel-check.sh" kernel
add "target-skeleton.mac-provisioning" "$ROOT/opensource/target-skeleton" 180 "bash tools/test-mac-provisioning.sh" fast
add "target-skeleton.mac-nvmem"      "$ROOT/opensource/target-skeleton" 180 "bash tools/test-mac-nvmem.sh" fast
add "upstream-series.gen-check"      "$ROOT/opensource/upstream/series-6.18" 180 "python3 tools/gen-series.py --check" fast
add "upstream-series.release-line"   "$HERE" 600 "./run-release-line-check.sh" external
add "platform.check-lab-src"         "$ROOT" 180 "bash tools/check-lab-src.sh" fast
add "platform.check-init-shadow"     "$ROOT" 180 "bash tools/check-init-shadow.sh" fast
add "platform.bench-inventory-tests" "$ROOT" 180 "bash tools/tests/bench-inventory.test.sh" fast

if [ "$MODE" = "list" ]; then
  printf '%s' "$ENTRIES" | sed '/^$/d' | cut -d'|' -f1,3,5
  exit 0
fi
if [ -n "$ONLY" ]; then
  FILTERED=$(printf '%s' "$ENTRIES" | sed '/^$/d' | grep -F "$ONLY" || true)
  [ -n "$FILTERED" ] || { echo "no entry matches --only $ONLY" >&2; exit 2; }
  ENTRIES="$FILTERED
"
fi
printf '%s' "$ENTRIES" | sed '/^$/d' | while IFS='|' read -r name dir tmo cmd cls; do
  if [ "$FAST" = 1 ] && [ "$cls" = "kernel" ]; then
    printf '%s\tSKIP\t0\tfast-mode skips kernel builds\n' "$name" >> "$SUMMARY"
    continue
  fi
  if [ "$EXT" = 0 ] && [ "$cls" = "external" ]; then
    printf '%s\t77\t0\tlocal OpenWrt tarball prerequisite (run with --external)\n' "$name" >> "$SUMMARY"
    continue
  fi
  log="$OUT/$name.log"
  start=$(date +%s)
  ( cd "$dir" && timeout "$tmo" bash -c "$cmd" ) >"$log" 2>&1
  rc=$?
  dur=$(( $(date +%s) - start ))
  pass=$(grep -oE '[0-9]+/[0-9]+ (checks?|tests?|assertions?|PASS|check)' "$log" | tail -1 | sed 's/ check.*//')
  [ -z "$pass" ] && pass=$(grep -oE '[0-9]+ passed' "$log" | tail -1)
  [ -z "$pass" ] && pass="-"
  printf '%s\t%s\t%s\t%s\n' "$name" "$rc" "$dur" "$pass" >> "$SUMMARY"
  if [ "$rc" = 0 ]; then st=PASS; elif [ "$rc" = 124 ]; then st=TIMEOUT; elif [ "$rc" = 77 ]; then st=SKIP; else st=FAIL; fi
  printf '%-42s %-7s %5ss  %s\n' "$name" "$st" "$dur" "$pass"
done

{
  echo "# host-only verifier suite result"
  echo "- generated: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "- runner sha256: $(sha256sum "$HERE/run-host-suite.sh" | cut -d' ' -f1)"
  echo
  echo "| entry | rc | s | pass-count |"
  echo "| --- | --- | --- | --- |"
  awk -F'\t' '{printf "| %s | %s | %s | %s |\n", $1, $2, $3, $4}' "$SUMMARY"
  echo
  total=$(wc -l < "$SUMMARY")
  pass=$(awk -F'\t' '$2==0' "$SUMMARY" | wc -l)
  fail=$(awk -F'\t' '$2!=0 && $2!="SKIP" && $2!=77' "$SUMMARY" | wc -l)
  skip=$(awk -F'\t' '$2=="SKIP" || $2==77' "$SUMMARY" | wc -l)
  echo "TOTAL $total  PASS $pass  FAIL $fail  SKIP $skip"
} > "$OUT/RESULT.txt"
cat "$OUT/RESULT.txt"
awk -F'\t' '$2!=0 && $2!="SKIP" && $2!=77' "$SUMMARY" | wc -l | grep -q '^0$'
