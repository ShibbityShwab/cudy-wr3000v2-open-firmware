#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# rx-capture-host.sh - HOST-side orchestrator for the decisive MAC-RX capture
# experiment (E-RX-1) on the Cudy WR3000 v2.0 bench (shared task-165).
#
# The LEAD runs this on the bench workstation. It pushes the READ-ONLY router
# script, starts the router sampler, triggers the proven client auth-frame
# path, stops the sampler, pulls the evidence bundle and runs the analyzer.
# The router side never writes device state.
#
# usage:
#   rx-capture-host.sh preflight
#   rx-capture-host.sh run [--label NAME] [--attempts N] [--seconds S]
#                          [--hz H] [--profile open|wpa2] [--skip-client]
#   rx-capture-host.sh analyze --dir DIR
#   rx-capture-host.sh selftest
#
# env: ROUTER_HOST (default root@192.168.10.1), RSH (default tools/rsh.sh),
#      RSCP (default tools/rscp.sh), ASSOC_HARNESS (default this repo's
#      wifi-assoc-e2e harness), ROUTER_SCRIPT (default this lane's router
#      script), RX_PROFILE (open|wpa2, default open).
# exit codes: 0 PASS, 1 usage, 2 preflight/refused, 3 crash detected on router
set -u

find_repo() {
  d=$(cd "$(dirname "$0")" && pwd)
  i=0
  while [ "$i" -lt 10 ]; do
    if [ -x "$d/tools/rsh.sh" ] && [ -f "$d/AGENTS.md" ]; then printf '%s\n' "$d"; return 0; fi
    d=$(cd "$d/.." && pwd)
    i=$((i + 1))
  done
  return 1
}
REPO=$(find_repo) || { echo "REFUSED: cannot locate the project repo root (tools/rsh.sh)" >&2; exit 2; }
set +u
ROUTER_HOST=$ROUTER_HOST
RSH=$RSH
RSCP=$RSCP
HARNESS=$ASSOC_HARNESS
RSCRIPT=$ROUTER_SCRIPT
PROFILE=$RX_PROFILE
set -u
[ -n "$ROUTER_HOST" ] || ROUTER_HOST=root@192.168.10.1
[ -n "$RSH" ] || RSH=$REPO/tools/rsh.sh
[ -n "$RSCP" ] || RSCP=$REPO/tools/rscp.sh
[ -n "$HARNESS" ] || HARNESS=$REPO/opensource/docs/incoming/wifi-assoc-e2e/scripts/assoc_e2e.py
[ -n "$RSCRIPT" ] || RSCRIPT=$REPO/opensource/docs/incoming/wifi-rx-capture-plan/scripts/router/rx-capture-router.sh
[ -n "$PROFILE" ] || PROFILE=open
ANALYZER=$REPO/opensource/docs/incoming/wifi-rx-capture-plan/scripts/host/analyze_capture.py
ROUTER_TMP=/tmp/rx-capture-router.sh
RXROOT=/tmp/rx-capture

say()  { printf '%s\n' "$*"; }
warn() { printf 'WARN: %s\n' "$*" >&2; }
die()  { printf 'REFUSED: %s\n' "$*" >&2; exit 2; }
usage() { sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; }
have() { command -v "$1" >/dev/null 2>&1; }
now_iso() { date -u +%Y-%m-%dT%H:%M:%SZ; }
now_epoch() { date +%s; }
step() { printf '\n=== %s ===\n' "$*"; }
rsh_do() { "$RSH" "$@" 2>&1; }

preflight() {
  step "host preflight"
  say "repo:     $REPO"
  say "router:   $ROUTER_HOST"
  say "rsh:      $RSH"
  say "rscp:     $RSCP"
  say "harness:  $HARNESS"
  say "rscript:  $RSCRIPT"
  say "analyzer: $ANALYZER"
  have python3 || die "python3 missing on the host"
  [ -f "$RSCRIPT" ] || die "router script missing: $RSCRIPT"
  [ -f "$HARNESS" ] || die "client harness missing: $HARNESS"
  [ -f "$ANALYZER" ] || die "analyzer missing: $ANALYZER"
  [ -x "$RSH" ] || die "rsh not executable: $RSH"
  [ -x "$RSCP" ] || die "rscp not executable: $RSCP"
  if have iw && have ip; then
    iface=$(iw dev 2>/dev/null | awk '/Interface/{print $2; exit}')
    [ -n "$iface" ] && say "host wlan: $iface (iw/ip present)"
  else
    warn "iw/ip not found on the host - the client trigger needs them"
  fi
  if sudo -n true >/dev/null 2>&1; then
    say "sudo: passwordless OK (client trigger can run)"
  else
    warn "sudo -n is not passwordless: the client trigger needs root; run this script as root or pre-authorize sudo"
  fi
  sh -n "$RSCRIPT" || die "router script does not parse"
  "$RSCRIPT" selftest >/dev/null || die "router script selftest failed"
  python3 "$ANALYZER" selftest >/dev/null || die "analyzer selftest failed"
  say "PREFLIGHT PASS"
}

push_router_script() {
  step "push router script"
  "$RSCP" "$RSCRIPT" "$ROUTER_HOST:$ROUTER_TMP" >/dev/null || die "failed to copy the router script to $ROUTER_TMP"
  rsh_do "chmod 755 $ROUTER_TMP && sha256sum $ROUTER_TMP" || die "cannot chmod the router script on the router"
}

router_check() {
  step "router check (read-only)"
  out=$1
  "$RSH" "sh $ROUTER_TMP check" 2>&1 | tee "$out"
  if grep -q 'CHECK PASS' "$out"; then :; else die "router check failed - see $out (do not trigger the client)"; fi
}

client_trigger() {
  attempts=$1
  step "client trigger (auth/probe frames at the AP BSSID)"
  have iw || die "iw missing on the host"
  python3 "$HARNESS" reset >"$OUTDIR/client-reset.txt" 2>&1 || warn "client reset reported an issue (continuing)"
  python3 "$HARNESS" scan --profile "$PROFILE" >"$OUTDIR/client-scan.txt" 2>&1 || warn "scan reported an issue (continuing)"
  {
    echo "client baseline epoch=$(now_epoch) iso=$(now_iso)"
    grep -E 'BSS|signal|SSID|cudy|06:05:04:03:02:01' "$OUTDIR/client-scan.txt" 2>/dev/null | head -20
  } >>"$MARKERS"
  i=1
  while [ "$i" -le "$attempts" ]; do
    step "client auth attempt $i/$attempts"
    echo "client auth attempt $i start epoch=$(now_epoch) iso=$(now_iso)" >>"$MARKERS"
    sudo -n python3 "$HARNESS" assoc --profile "$PROFILE" >"$OUTDIR/client-assoc-$i.txt" 2>&1
    rc=$?
    echo "client auth attempt $i end epoch=$(now_epoch) rc=$rc" >>"$MARKERS"
    say "attempt $i rc=$rc (see client-assoc-$i.txt)"
    if [ "$rc" != 3 ]; then sleep 2; fi
    i=$((i + 1))
  done
  sudo -n sh -c "dmesg | grep -i 'send auth' | tail -20" >"$OUTDIR/client-dmesg-auth.txt" 2>/dev/null || true
  say "client done - stopping the router sampler"
}

do_run() {
  label=""; attempts=4; seconds=90; hz=5; skip_client=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --label) shift; label=$1 ;;
      --attempts) shift; attempts=$1 ;;
      --seconds) shift; seconds=$1 ;;
      --hz) shift; hz=$1 ;;
      --profile) shift; PROFILE=$1 ;;
      --skip-client) skip_client=1 ;;
      *) die "run: unknown option $1" ;;
    esac
    shift
  done
  preflight
  [ -n "$label" ] || label=$(date -u +%Y%m%dT%H%M%SZ)
  case "$label" in ''|*[!A-Za-z0-9._-]*) die "invalid label" ;; esac
  OUTDIR=$REPO/build/wifi-rx-capture/$label
  mkdir -p "$OUTDIR" || die "cannot create $OUTDIR"
  MARKERS=$OUTDIR/host-markers.log
  : >"$MARKERS"
  say "evidence: $OUTDIR"
  push_router_script
  router_check "$OUTDIR/router-check.txt" || exit 2
  step "router pre snapshot"
  rsh_do "sh $ROUTER_TMP snapshot $label-pre" >"$OUTDIR/router-pre.txt" 2>&1 || true
  step "start router sampler"
  rsh_do "sh $ROUTER_TMP run $label --seconds $seconds --hz $hz --daemon" | tee "$OUTDIR/router-sampler-start.txt"
  grep -q 'SAMPLER STARTED' "$OUTDIR/router-sampler-start.txt" || die "router sampler did not start"
  sleep 2
  if [ "$skip_client" = 0 ]; then
    client_trigger "$attempts"
  else
    say "SKIP-CLIENT: recording the requested idle window only"
    sleep "$seconds"
  fi
  step "stop router sampler"
  rsh_do "sh $ROUTER_TMP stop $label" | tee "$OUTDIR/router-sampler-stop.txt" || true
  step "router post snapshot"
  rsh_do "sh $ROUTER_TMP snapshot $label-post" >"$OUTDIR/router-post.txt" 2>&1 || true
  crash=0
  if grep -q 'CRASH: YES' "$OUTDIR/router-pre.txt" "$OUTDIR/router-post.txt" 2>/dev/null; then crash=1; fi
  step "collect and pull the bundle"
  rsh_do "sh $ROUTER_TMP collect $label" | tee "$OUTDIR/router-collect.txt"
  bundle=0
  if "$RSCP" "$ROUTER_HOST:$RXROOT/$label.tar.gz" "$OUTDIR/router-bundle.tar.gz" >/dev/null 2>&1; then
    if tar -xzf "$OUTDIR/router-bundle.tar.gz" -C "$OUTDIR" 2>/dev/null; then bundle=1; fi
  fi
  [ "$bundle" = 1 ] || warn "bundle pull/extract failed - analysis uses the router-side text snapshots in $OUTDIR"
  step "analysis"
  if [ "$bundle" = 1 ]; then
    python3 "$ANALYZER" --dir "$OUTDIR" | tee "$OUTDIR/analysis.txt"
    arc=$?
  else
    say "no bundle - classification: V-CRASH (see $OUTDIR/router-post.txt)"
    arc=3
  fi
  if [ "$crash" = 1 ]; then
    warn "the router reported a firmware exception during/around the capture - verdict: V-CRASH"
    exit 3
  fi
  if [ "$arc" = 0 ]; then say ""; say "RUN COMPLETE - verdict: $OUTDIR/analysis.txt"; exit 0; fi
  exit "$arc"
}

do_analyze() {
  dir=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --dir) shift; dir=$1 ;;
      *) die "analyze: unknown option $1" ;;
    esac
    shift
  done
  [ -n "$dir" ] || die "analyze needs --dir DIR"
  python3 "$ANALYZER" --dir "$dir"
  exit $?
}

do_selftest() {
  rc=0
  sh -n "$0" 2>/dev/null || { echo "FAIL shell syntax"; rc=1; }
  [ -f "$RSCRIPT" ] && echo "ok   router script present" || { echo "FAIL router script missing"; rc=1; }
  [ -f "$ANALYZER" ] && echo "ok   analyzer present" || { echo "FAIL analyzer missing"; rc=1; }
  python3 "$ANALYZER" selftest >/dev/null 2>&1 && echo "ok   analyzer selftest" || { echo "FAIL analyzer selftest"; rc=1; }
  sh -n "$0" && echo "ok   host script parses"
  [ "$rc" = 0 ] && echo "HOST SELFTEST PASS"
  exit "$rc"
}

[ $# -ge 1 ] || { usage; exit 1; }
cmd=$1; shift
case "$cmd" in
  --help|-h|help) usage; exit 0 ;;
  preflight) preflight ;;
  run) do_run "$@" ;;
  analyze) do_analyze "$@" ;;
  selftest) do_selftest ;;
  *) usage; die "unknown command '$cmd'" ;;
esac
exit 0

