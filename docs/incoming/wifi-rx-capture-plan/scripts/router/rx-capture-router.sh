#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# rx-capture-router.sh - ROUTER-SIDE, READ-ONLY RX-capture instrument for the
# Cudy WR3000 v2.0 (Hi5622V100) MAC-RX investigation (shared task-165).
#
# RUN THIS ON THE BENCH ROUTER, BY THE LEAD. The host-only lane that authored
# it NEVER executes it against the router.
#
# Safety contract (enforced by construction):
#   * READ-ONLY. The script reads sysfs attributes, dmesg, /proc, iw state and
#     (optionally) /dev/mem with O_RDONLY. It never writes a device register,
#     never writes a /dev/mem location, never loads/unloads a module, never
#     changes a module parameter, never reboots and never touches flash.
#   * The only device-visible action besides reads is --allow-oneshot, which
#     writes "1" to the driver's documented READ-ONLY sysfs triggers
#     (dp_rx_arm / dp_rx_poll). Those actions only read the live MAC bank; they
#     are one-shot per module load. Default: disabled (the raw /dev/mem sampler
#     is used instead).
#   * Everything the script creates lives under RX_CAPTURE_ROOT (default
#     /tmp/rx-capture) and is evidence, not device state.
#
# Captures: one-shot sysfs evidence (boot/hcc/dp/cfg80211/rd_*), the live
# host-MAC bank CA 0x40050000 (+0x00 status, +0x08/+0x0c free-rptr, +0x10
# completion wptr), the config block CA 0x40040000 (+0x44 W1C, +0x48 mask,
# ring pointers), the firmware exception page/mask/context, the RX ring MEMORY
# and every published buffer's 36-byte device header, the D2H catalogue, and a
# crash watch that stops the experiment on the first page/mask/staging
# deviation. See REPORT.md for the read matrix and the decode tables.
#
# usage:
#   rx-capture-router.sh check
#   rx-capture-router.sh snapshot LABEL [--no-headers] [--header-limit N]
#   rx-capture-router.sh run LABEL [--seconds N] [--hz H] [--daemon]
#   rx-capture-router.sh stop LABEL
#   rx-capture-router.sh status [LABEL]
#   rx-capture-router.sh collect LABEL
#   rx-capture-router.sh selftest
#
# env: RX_CAPTURE_ROOT (evidence dir), RX_DEV (PCI sysfs dir), RX_ALLOW_ONESHOT=1,
#      RX_RAW=0|1 (force raw availability)
# exit codes: 0 OK, 1 usage, 2 refused/precondition, 3 crash detected,
#             4 inconclusive (no raw /dev/mem and no one-shot fallback)
set -u

# env import (plain assignments so the safety surface stays greppable)
set +u
RXROOT=$RX_CAPTURE_ROOT
ONESHOT=$RX_ALLOW_ONESHOT
RAWFORCE=$RX_RAW
DEVOVR=$RX_DEV
set -u
[ -n "$RXROOT" ] || RXROOT=/tmp/rx-capture
[ -n "$ONESHOT" ] || ONESHOT=0
[ -n "$RAWFORCE" ] || RAWFORCE=auto

RX_VERSION=1

# fixed project addresses (derived facts; REPORT.md section 4)
IO_BASE=0x3b8000
CA_LIVE=0x40050000
CA_CFG=0x40040000
OFF_LIVE=0x408000
OFF_CFG=0x3f8000
OFF_PAGE=0x877000
OFF_STATE=0x7d0b60
OFF_EXCP_MASK=0x6f8008
OFF_EXCP_FRAME=0x6f800c
LIVE_STATUS=0x00
LIVE_NORM_RPTR=0x08
LIVE_SMALL_RPTR=0x0c
LIVE_CMP_WPTR=0x10
CFG_CMP_RPTR=0x40
CFG_INT_STATUS=0x44
CFG_INT_MASK=0x48
GOLDEN0=86807fef
RAM_LO=0x80000000
RAM_HI=0x8fffffff
NORM_DEPTH=256
SMALL_DEPTH=512
CMP_DEPTH=4095
BUF_READ=64
INT_MASK_INIT=fffffcc1

say()  { printf '%s\n' "$*"; }
warn() { printf 'WARN: %s\n' "$*" >&2; }
die()  { printf 'REFUSED: %s\n' "$*" >&2; exit 2; }
have() { command -v "$1" >/dev/null 2>&1; }
now_epoch() { date +%s 2>/dev/null || echo 0; }
now_iso() { date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date 2>/dev/null; }
uptime_s() { cut -d. -f1 /proc/uptime 2>/dev/null | tr -d ' '; }
usage() { sed -n '2,64p' "$0" | sed 's/^# \{0,1\}//'; }
pfx() { if [ -n "$1" ]; then printf '0x%s' "$1"; else printf '-'; fi; }

# host-MAC interrupt status bit map (vendor ISR decode; REPORT.md section 5)
bit_names() {
  in=$1; out=""
  i=0
  while [ "$i" -le 10 ]; do
    if [ $((in & (1 << i))) -ne 0 ]; then
      case "$i" in
        0) n=tx_complete_alias ;;
        1) n=tx_complete ;;
        2) n=RX_complete ;;
        3) n=norm_ring_empty ;;
        4) n=small_ring_empty ;;
        5) n=data_ring_overrun ;;
        6) n=location_complete_error ;;
        7) n=ba_info_ring_overrun ;;
        8) n=ba_info_ring_overrun ;;
        9) n=ba_win_exceed_error ;;
        10) n=rx_ppdu_desc_error ;;
        *) n=unknown ;;
      esac
      out="$out bit$i=$n"
    fi
    i=$((i + 1))
  done
  [ -z "$out" ] && out=" none"
  printf '%s' "$out"
}


# ---- device / attribute discovery ---------------------------------------
D=""
find_dev() {
  if [ -n "$DEVOVR" ] && [ -d "$DEVOVR" ]; then D=$DEVOVR; return 0; fi
  for cand in /sys/bus/pci/drivers/hi5622_wifi/0000:* /sys/bus/pci/drivers/hi5622_wifi/*:*; do
    [ -d "$cand" ] && { D=$cand; return 0; }
  done
  for cand in /sys/bus/pci/devices/*; do
    [ -d "$cand" ] || continue
    if [ -e "$cand/dp_rx_arm" ] || [ -e "$cand/dp_status" ]; then D=$cand; return 0; fi
  done
  for cand in /sys/bus/pci/devices/0000:00:00.0 /sys/bus/pci/devices/0000:01:00.0; do
    [ -d "$cand" ] && { D=$cand; return 0; }
  done
  return 1
}
attr_path() { printf '%s/%s' "$D" "$1"; }
have_attr() { [ -e "$(attr_path "$1")" ]; }
read_attr() { [ -r "$(attr_path "$1")" ] && cat "$(attr_path "$1")" 2>/dev/null; }
read_param() { cat "/sys/module/hi5622_wifi/parameters/$1" 2>/dev/null; }
module_loaded() { grep -q '^hi5622_wifi ' /proc/modules 2>/dev/null; }
bar0_phys() {
  [ -n "$D" ] && [ -r "$D/resource" ] || return 1
  sed -n '1p' "$D/resource" 2>/dev/null | awk '{print $1}'
}

# ---- raw /dev/mem access (READ-ONLY; the fd is opened read-only) -------
RAW_OK=0
RAW_WHY=""
RAW_BP=""
raw_words_chk() {
  if have od; then
    dd if=/dev/mem bs=4 skip=$((PHYS / 4)) count="$COUNT" 2>/dev/null | od -An -v -tx4 2>/dev/null | tr -s ' \t' '\n\n' | grep -v '^$'
  else
    dd if=/dev/mem bs=4 skip=$((PHYS / 4)) count="$COUNT" 2>/dev/null | hexdump -v -e '1/4 "%08x\n"' 2>/dev/null
  fi
}
raw_words() {
  [ "$RAW_OK" = 1 ] || return 1
  PHYS=$1; COUNT=$2
  raw_words_chk
}
raw_one() { PHYS=$1; COUNT=1; raw_words_chk | sed -n '1p'; }
raw_hexbytes() {
  [ "$RAW_OK" = 1 ] || return 1
  phys=$1; count=$2
  if have od; then
    dd if=/dev/mem bs=1 skip="$phys" count="$count" 2>/dev/null | od -An -v -tx1 2>/dev/null | tr -d ' \t\n\r'
  else
    dd if=/dev/mem bs=1 skip="$phys" count="$count" 2>/dev/null | hexdump -v -e '1/1 "%02x"' 2>/dev/null
  fi
}
raw_probe() {
  RAW_OK=0
  if [ "$RAWFORCE" = 0 ]; then RAW_WHY="raw disabled by RX_RAW=0"; return 1; fi
  [ -r /dev/mem ] || { RAW_WHY="/dev/mem not readable (CONFIG_DEVMEM?)"; return 1; }
  have dd || { RAW_WHY="dd missing"; return 1; }
  if ! have od && ! have hexdump; then RAW_WHY="neither od nor hexdump present"; return 1; fi
  RAW_BP=$(bar0_phys) || { RAW_WHY="BAR0 resource start not readable"; return 1; }
  case "$RAW_BP" in 0x*|0X*) ;; *) RAW_WHY="BAR0 resource start '$RAW_BP' is not hex"; return 1 ;; esac
  p=$(raw_one $((RAW_BP + OFF_PAGE)))
  [ -n "$p" ] || { RAW_WHY="first /dev/mem read returned nothing"; return 1; }
  if [ "$p" = "$GOLDEN0" ]; then RAW_OK=1; RAW_WHY="raw OK (golden page read)"; return 0; fi
  v=$(raw_one $((RAW_BP + OFF_LIVE)))
  if [ -n "$v" ] && [ "$v" != "ffffffff" ]; then
    RAW_OK=1; RAW_WHY="raw OK (page not golden, live bank readable)"; return 0
  fi
  RAW_WHY="raw read returned 0xffffffff at BAR0 live bank (iATU window down or /dev/mem blocked)"
  return 1
}

# optional one-shot READ-ONLY driver triggers (documented no-device-write)
oneshot_trigger() {
  [ "$ONESHOT" = 1 ] || return 0
  a=$1
  have_attr "$a" || return 0
  [ -w "$(attr_path "$a")" ] || return 0
  printf '1' >"$(attr_path "$a")" 2>/dev/null || true
}

# ---- sysfs snapshot helpers --------------------------------------------
snapshot_sysfs() {
  dir=$1
  mkdir -p "$dir/sysfs"
  for a in boot hcc_status hcc_ready hcc_events hcc_rings dev_status \
           dp_status dp_rings dp_tx_status dp_rx_capture dp_mac_ext \
           cfg80211_status data_tx_status rd_status rd_catalog rd_decode; do
    if have_attr "$a"; then read_attr "$a" >"$dir/sysfs/$a.txt" 2>/dev/null; fi
  done
  for p in /sys/module/hi5622_wifi/parameters/*; do
    [ -r "$p" ] && printf '%s=%s\n' "$(basename "$p")" "$(cat "$p" 2>/dev/null)"
  done >"$dir/sysfs/module-params.txt" 2>/dev/null
  dmesg >"$dir/dmesg.txt" 2>/dev/null || true
  if have iw; then
    {
      iw dev 2>/dev/null
      echo '---'
      iw dev 2>/dev/null | awk '/Interface/{print $2}' | while read -r w; do
        echo "## $w info"
        iw dev "$w" info 2>/dev/null
        echo "## $w station dump"
        iw dev "$w" station dump 2>/dev/null
      done
    } >"$dir/wireless-state.txt" 2>&1
  fi
  { ps 2>/dev/null | grep -E 'hostapd|drain' | grep -v grep; } >"$dir/processes.txt" 2>&1
}


# ===========================================================================
# check
# ===========================================================================
do_check() {
  mkdir -p "$RXROOT" || die "cannot create $RXROOT"
  out=$RXROOT/check.txt
  {
    echo "== rx-capture-router check v$RX_VERSION =="
    echo "iso: $(now_iso)"
    echo "kernel: $(uname -r 2>/dev/null)"
    echo "uptime_s: $(uptime_s)"
    if module_loaded; then echo "module: loaded"; else echo "module: NOT loaded"; fi
    if find_dev; then echo "device: $D"; else echo "device: NOT FOUND"; fi
    echo "params:"
    for p in rx_enable rx_clear_status rx_drain_max rx_refill_wm mgmt_tx mgmt_rx data_tx eapol_rx h2d_send_budget hcc_trace; do
      v=$(read_param "$p")
      [ -n "$v" ] && echo "  $p=$v"
    done
    echo "one-shot read triggers present:"
    for a in dp_rx_arm dp_rx_poll dp_rx_drain; do
      if have_attr "$a"; then echo "  $a"; else echo "  MISSING $a"; fi
    done
    echo "read-only attributes:"
    for a in boot hcc_status hcc_events hcc_ready hcc_rings dev_status dp_status dp_rings dp_tx_status dp_rx_capture dp_mac_ext cfg80211_status data_tx_status rd_status rd_catalog rd_decode; do
      if have_attr "$a"; then echo "  $a"; else echo "  MISSING $a"; fi
    done
    if [ -n "$D" ]; then echo "bar0 resource: $(sed -n '1p' "$D/resource" 2>/dev/null)"; fi
    if raw_probe; then
      echo "raw /dev/mem: AVAILABLE ($RAW_WHY) bar0_phys=$RAW_BP"
    else
      echo "raw /dev/mem: UNAVAILABLE ($RAW_WHY)"
    fi
    echo "== boot state =="
    read_attr boot | sed 's/^/  /'
    echo "== hcc_events (first line) =="
    read_attr hcc_events | sed -n '1p' | sed 's/^/  /'
    echo "== drain/hostapd processes =="
    ps 2>/dev/null | grep -E 'hostapd|drain' | grep -v grep | sed 's/^/  /'
    echo "== plan =="
    echo "  address map: io_base=$IO_BASE live_ca=$CA_LIVE cfg_ca=$CA_CFG mask_init=$INT_MASK_INIT"
    echo "  live fields: status=$LIVE_STATUS norm_rptr=$LIVE_NORM_RPTR small_rptr=$LIVE_SMALL_RPTR cmp_wptr=$LIVE_CMP_WPTR"
    echo "  cfg fields: cmp_rptr=$CFG_CMP_RPTR int_status=$CFG_INT_STATUS int_mask=$CFG_INT_MASK"
    echo "  E-RX-1 requires: module loaded, rx_enable=1, AP-ENABLED, dp_setup + dp_txq_addr_sync + dp_rx_arm done, NO drain loop."
    echo "  If a drain loop runs, stop it first; this script never stops it."
  } 2>&1 | tee "$out"
  echo
  echo "check evidence: $out"
  module_loaded || die "hi5622_wifi not loaded - run the association ladder first (the Lead fires)"
  find_dev || die "no hi5622_wifi PCI device"
  [ "$(read_param rx_enable)" = 1 ] || die "rx_enable != 1 - the live RX bank is only valid with rx_enable=1"
  have_attr dp_rx_arm || die "dp_rx_arm missing - wrong module (need the rx-static/rx-watermark association module)"
  have_attr boot || die "boot attribute missing - wrong module"
  if ! read_attr boot | grep -q 'golden:1'; then
    warn "boot page is not golden: record it, do not fire E-RX-1 blind"
  fi
  if ! read_attr hcc_events | sed -n '1p' | grep -q 'excp_announce=0'; then
    warn "excp_announce is not 0: the firmware may already be exceptional"
  fi
  if raw_probe; then
    echo "RAW PASS"
  elif [ "$ONESHOT" = 1 ]; then
    echo "RAW SKIP: --allow-oneshot one-shot sysfs sampling enabled (limited evidence)"
  else
    echo "RAW SKIP: no raw sampling; rerun with --allow-oneshot for the one-shot sysfs fallback" >&2
  fi
  echo "CHECK PASS"
}


# ===========================================================================
# snapshot - one full read-only evidence bundle under RXROOT/snap-LABEL
# ===========================================================================
do_snapshot() {
  label=$1; shift
  no_headers=0; hdr_limit=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --no-headers) no_headers=1 ;;
      --header-limit) shift; hdr_limit=$1 ;;
      *) die "snapshot: unknown option $1" ;;
    esac
    shift
  done
  case "$label" in ''|*[!A-Za-z0-9._-]*) die "invalid label (use [A-Za-z0-9._-])" ;; esac
  dir=$RXROOT/snap-$label
  rm -rf "$dir"
  mkdir -p "$dir/raw" "$dir/rings" "$dir/headers" "$dir/sysfs" || die "cannot create $dir"
  BP=$(bar0_phys 2>/dev/null)
  raw_probe >/dev/null 2>&1 || true
  {
    echo "label: $label"
    echo "iso: $(now_iso)"
    echo "epoch: $(now_epoch)"
    echo "uptime_s: $(uptime_s)"
    echo "kernel: $(uname -r 2>/dev/null)"
    echo "device: $D"
    echo "bar0_phys: $BP"
    echo "raw_ok: $RAW_OK ($RAW_WHY)"
    echo "cmdline: $(cat /proc/cmdline 2>/dev/null)"
  } >"$dir/meta.txt"
  snapshot_sysfs "$dir"

  if [ "$RAW_OK" = 1 ]; then
    raw_words $((BP + OFF_LIVE)) 5 >"$dir/raw/live-bank.txt" 2>/dev/null
    raw_words $((BP + OFF_LIVE)) 256 >"$dir/raw/live-window-0x40050000.txt" 2>/dev/null
    raw_words $((BP + OFF_CFG)) 256 >"$dir/raw/config-window-0x40040000.txt" 2>/dev/null
    raw_words $((BP + OFF_PAGE)) 8 >"$dir/raw/page.txt" 2>/dev/null
    raw_words $((BP + OFF_STATE)) 1 >"$dir/raw/state.txt" 2>/dev/null
    raw_words $((BP + OFF_EXCP_FRAME)) 17 >"$dir/raw/excp-frame.txt" 2>/dev/null
    raw_words $((BP + OFF_EXCP_MASK)) 1 >"$dir/raw/excp-mask.txt" 2>/dev/null
    st=$(sed -n '1p' "$dir/raw/live-bank.txt" 2>/dev/null)
    nr=$(sed -n '3p' "$dir/raw/live-bank.txt" 2>/dev/null)
    sr=$(sed -n '4p' "$dir/raw/live-bank.txt" 2>/dev/null)
    cw=$(sed -n '5p' "$dir/raw/live-bank.txt" 2>/dev/null)
    mk=$(sed -n '1p' "$dir/raw/excp-mask.txt" 2>/dev/null)
    sb=""
    if [ -n "$st" ]; then sb=$(bit_names $((0x$st))); fi
    {
      echo "live_status=$(pfx "$st") norm_rptr=$(pfx "$nr") small_rptr=$(pfx "$sr") cmp_wptr=$(pfx "$cw")"
      echo "live_status_bits:$sb"
      echo "excp_mask=$(pfx "$mk")"
    } >"$dir/raw/summary.txt"
  else
    echo "raw /dev/mem unavailable: $RAW_WHY" >"$dir/raw/UNAVAILABLE.txt"
    oneshot_trigger dp_rx_arm
    oneshot_trigger dp_rx_poll
    dmesg 2>/dev/null | grep -E 'DP rx_arm|DP rx_poll' | tail -5 >"$dir/raw/oneshot-dmesg.txt" 2>/dev/null || true
  fi

  if [ "$RAW_OK" = 1 ]; then
    rings=$(read_attr dp_status | grep 'host rings:' | head -1)
    norm=$(printf '%s\n' "$rings" | sed -n 's/.*norm_ring=\(0x[0-9a-fA-F]*\).*/\1/p')
    small=$(printf '%s\n' "$rings" | sed -n 's/.*small_ring=\(0x[0-9a-fA-F]*\).*/\1/p')
    cmp=$(printf '%s\n' "$rings" | sed -n 's/.*cmp_ring=\(0x[0-9a-fA-F]*\).*/\1/p')
    {
      echo "host_rings_line: $rings"
      echo "norm_ring_devva=$norm"
      echo "small_ring_devva=$small"
      echo "cmp_ring_devva=$cmp"
    } >"$dir/rings/meta.txt"
    for kind in norm small cmp; do
      case "$kind" in
        norm) va=$norm; depth=$NORM_DEPTH ;;
        small) va=$small; depth=$SMALL_DEPTH ;;
        cmp) va=$cmp; depth=$CMP_DEPTH ;;
      esac
      if [ -z "$va" ]; then echo "$kind: devVA not found in dp_status" >>"$dir/rings/meta.txt"; continue; fi
      vad=$((va))
      if [ "$vad" -lt "$RAM_LO" ] || [ "$vad" -gt "$RAM_HI" ]; then
        echo "$kind: devVA $va outside device host-DRAM window - skipped" >>"$dir/rings/meta.txt"
        continue
      fi
      raw_words "$vad" "$depth" >"$dir/rings/$kind.txt" 2>/dev/null
      nz=0
      idx=0
      : >"$dir/rings/$kind-nonzero.txt"
      while read -r w; do
        if [ "$w" != "00000000" ] && [ -n "$w" ]; then
          nz=$((nz + 1))
          printf '%s 0x%s\n' "$idx" "$w" >>"$dir/rings/$kind-nonzero.txt"
        fi
        idx=$((idx + 1))
      done <"$dir/rings/$kind.txt"
      echo "$kind: depth=$depth nonzero=$nz" >>"$dir/rings/meta.txt"
    done
    if [ "$no_headers" = 0 ]; then
      for kind in norm small; do
        f="$dir/rings/$kind.txt"
        [ -s "$f" ] || continue
        n=0
        : >"$dir/headers/$kind.txt"
        while read -r w; do
          [ -n "$w" ] || continue
          n=$((n + 1))
          if [ "$hdr_limit" -gt 0 ] && [ "$n" -gt "$hdr_limit" ]; then break; fi
          e=$((0x$w))
          if [ "$e" -ge "$RAM_LO" ] && [ "$e" -le "$RAM_HI" ] && [ "$e" -ne 0 ]; then
            hex=$(raw_hexbytes "$e" "$BUF_READ" 2>/dev/null)
            printf '%s 0x%s %s\n' "$((n - 1))" "$w" "$hex" >>"$dir/headers/$kind.txt"
          fi
        done <"$f"
      done
      {
        for kind in norm small; do
          if [ -s "$dir/headers/$kind.txt" ]; then
            lines=$(wc -l <"$dir/headers/$kind.txt" | tr -d ' ')
            if have md5sum; then dg=$(md5sum "$dir/headers/$kind.txt" 2>/dev/null | cut -d' ' -f1); else dg=NA; fi
            echo "$kind lines=$lines md5=$dg"
          fi
        done
      } >"$dir/headers/digest.txt"
    fi
    p0=$(sed -n '1p' "$dir/raw/page.txt" 2>/dev/null)
    p8=$(sed -n '3p' "$dir/raw/page.txt" 2>/dev/null)
    mkv=$(sed -n '1p' "$dir/raw/excp-mask.txt" 2>/dev/null)
    stg=$(sed -n '1p' "$dir/raw/excp-frame.txt" 2>/dev/null)
    ex=$(read_attr hcc_events | sed -n '1p')
    {
      echo "page0=$(pfx "$p0") page8=$(pfx "$p8") excp_mask=$(pfx "$mkv") staging=$(pfx "$stg")"
      echo "hcc_events: $ex"
      if [ "$p0" = "$GOLDEN0" ] && [ "$p8" = "00000002" ] && [ "$mkv" = "00000000" ] && [ "$stg" = "00000000" ]; then
        echo "CRASH: no"
      else
        echo "CRASH: YES (deviation) - do not continue E-RX-1; power-cycle the Wi-Fi chip"
      fi
    } >"$dir/crash.txt"
  fi
  echo "snapshot: $dir"
  cat "$dir/meta.txt"
  [ -f "$dir/rings/meta.txt" ] && cat "$dir/rings/meta.txt"
  [ -f "$dir/raw/summary.txt" ] && cat "$dir/raw/summary.txt"
  [ -f "$dir/crash.txt" ] && cat "$dir/crash.txt"
}


# ===========================================================================
# run - timestamped read-only sampler + crash watch
# ===========================================================================
do_run_internal() {
  label=$1; seconds=$2; hz=$3
  dir=$RXROOT/snap-$label
  mkdir -p "$dir" || die "cannot create $dir"
  raw_probe >/dev/null 2>&1 || true
  BP=$(bar0_phys 2>/dev/null)
  base_page0=""; base_page8=""; base_mask=""; base_staging=""; base_excp=""
  if [ "$RAW_OK" = 1 ]; then
    base_page0=$(raw_one $((BP + OFF_PAGE)))
    base_page8=$(raw_one $((BP + OFF_PAGE + 8)))
    base_mask=$(raw_one $((BP + OFF_EXCP_MASK)))
    base_staging=$(raw_one $((BP + OFF_EXCP_FRAME)))
  fi
  base_excp=$(read_attr hcc_events | sed -n '1p' | sed -n 's/.*excp_announce=\([-0-9]*\).*/\1/p')
  {
    echo "# rx-capture-router sampler v$RX_VERSION"
    echo "# label=$label seconds=$seconds hz=$hz raw_ok=$RAW_OK why=$RAW_WHY"
    echo "# baseline page0=0x$base_page0 page8=0x$base_page8 mask=0x$base_mask staging=0x$base_staging excp_announce=$base_excp iso=$(now_iso)"
    printf '#tick\tepoch\tuptime_s\tpage0\tpage8\tgolden\tmask\tstaging\texcp_ann\tstatus\tstatus_bits\tnorm_rptr\tsmall_rptr\tcmp_wptr\n'
  } >"$dir/samples.tsv"
  : >"$dir/markers.log"
  start=$(now_epoch)
  stop_t=$((start + seconds))
  interval_ms=200
  if [ "$hz" -ge 1 ] 2>/dev/null; then interval_ms=$((1000 / hz)); fi
  [ "$interval_ms" -lt 50 ] && interval_ms=50
  sleep_s=$(awk "BEGIN{printf \"%.3f\", $interval_ms/1000}")
  t=0
  crash_seen=0
  while :; do
    now=$(now_epoch)
    if [ "$now" -ge "$stop_t" ]; then break; fi
    t=$((t + 1))
    p0=""; p8=""; mask=""; staging=""; status=""; nr=""; sr=""; cw=""; golden=""; ann=""
    if [ "$RAW_OK" = 1 ]; then
      p0=$(raw_one $((BP + OFF_PAGE)))
      p8=$(raw_one $((BP + OFF_PAGE + 8)))
      mask=$(raw_one $((BP + OFF_EXCP_MASK)))
      staging=$(raw_one $((BP + OFF_EXCP_FRAME)))
      lb=$(raw_words $((BP + OFF_LIVE)) 5 2>/dev/null)
      status=$(printf '%s\n' "$lb" | sed -n '1p')
      nr=$(printf '%s\n' "$lb" | sed -n '3p')
      sr=$(printf '%s\n' "$lb" | sed -n '4p')
      cw=$(printf '%s\n' "$lb" | sed -n '5p')
    fi
    b1=$(read_attr boot | sed -n '1p')
    case "$b1" in
      *"golden:1"*) golden=1 ;;
      *"golden:0"*) golden=0 ;;
    esac
    ann=$(read_attr hcc_events | sed -n '1p' | sed -n 's/.*excp_announce=\([-0-9]*\).*/\1/p')
    crash=0
    if [ "$golden" = 0 ]; then crash=1; fi
    if [ -n "$p0" ] && [ -n "$base_page0" ] && [ "$p0" != "$base_page0" ]; then crash=1; fi
    if [ -n "$mask" ] && [ -n "$base_mask" ] && [ "$mask" != "$base_mask" ]; then crash=1; fi
    if [ -n "$staging" ] && [ -n "$base_staging" ] && [ "$staging" != "$base_staging" ]; then crash=1; fi
    if [ -n "$ann" ] && [ "$ann" != 0 ]; then crash=1; fi
    if [ "$crash" = 1 ] && [ "$crash_seen" = 0 ]; then
      crash_seen=1
      {
        echo "CRASH FIRST DEVIATION tick=$t epoch=$now iso=$(now_iso)"
        echo "page0=0x$p0 baseline=0x$base_page0 page8=0x$p8 baseline=0x$base_page8"
        echo "mask=0x$mask baseline=0x$base_mask staging=0x$staging baseline=0x$base_staging excp_announce=$ann baseline=$base_excp"
        cb=""
        if [ -n "$status" ]; then cb=$(bit_names $((16#$status))); fi
        echo "live status=0x$status bits:$cb"
        echo "live norm_rptr=0x$nr small_rptr=0x$sr cmp_wptr=0x$cw"
        echo "== dmesg tail =="
        dmesg 2>/dev/null | tail -40
      } >"$dir/crash-first.txt"
      echo "CRASH tick=$t epoch=$now" >>"$dir/markers.log"
      stop_t=$((now + 10))
    fi
    bits=""
    if [ -n "$status" ]; then bits=$(bit_names $((16#$status))); fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$t" "$now" "$(uptime_s)" "$(pfx "$p0")" "$(pfx "$p8")" "$golden" \
      "$(pfx "$mask")" "$(pfx "$staging")" "$ann" "$(pfx "$status")" "$bits" \
      "$(pfx "$nr")" "$(pfx "$sr")" "$(pfx "$cw")" >>"$dir/samples.tsv"
    sleep "$sleep_s" 2>/dev/null || sleep 1
  done
  echo "SAMPLE END label=$label ticks=$t iso=$(now_iso) crash=$crash_seen" >>"$dir/markers.log"
  echo "sampler done: $dir/samples.tsv ticks=$t crash=$crash_seen"
  if [ "$crash_seen" = 1 ]; then exit 3; fi
  exit 0
}

do_run() {
  label=$1; shift
  seconds=120; hz=5; daemon=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --seconds) shift; seconds=$1 ;;
      --hz) shift; hz=$1 ;;
      --daemon) daemon=1 ;;
      *) die "run: unknown option $1" ;;
    esac
    shift
  done
  case "$label" in ''|*[!A-Za-z0-9._-]*) die "invalid label" ;; esac
  if [ "$daemon" = 0 ]; then
    do_run_internal "$label" "$seconds" "$hz"
    exit $?
  fi
  mkdir -p "$RXROOT/snap-$label" || die "cannot create snapshot dir"
  pidf=$RXROOT/snap-$label/sampler.pid
  if [ -s "$pidf" ] && kill -0 "$(cat "$pidf")" 2>/dev/null; then die "sampler already running"; fi
  nohup "$0" __sample "$label" "$seconds" "$hz" >"$RXROOT/snap-$label/sampler.out" 2>&1 &
  pid=$!
  echo "$pid" >"$pidf"
  sleep 1
  if kill -0 "$pid" 2>/dev/null; then
    echo "SAMPLER STARTED pid=$pid label=$label seconds=$seconds hz=$hz"
    echo "samples: $RXROOT/snap-$label/samples.tsv"
  else
    die "sampler failed to start (see $RXROOT/snap-$label/sampler.out)"
  fi
}

do_stop() {
  label=$1
  dir=$RXROOT/snap-$label
  pidf=$dir/sampler.pid
  [ -f "$pidf" ] || die "no sampler pid file at $pidf"
  pid=$(cat "$pidf" 2>/dev/null)
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    i=0
    while [ "$i" -lt 20 ] && kill -0 "$pid" 2>/dev/null; do i=$((i + 1)); sleep 1; done
    if kill -0 "$pid" 2>/dev/null; then warn "sampler pid $pid still alive"; fi
    echo "SAMPLER STOPPED pid=$pid"
  else
    echo "sampler not running (stale pid $pid)"
  fi
  rm -f "$pidf"
}

do_status() {
  label=$1
  if [ -z "$label" ]; then
    echo "RXROOT: $RXROOT"
    ls -1 "$RXROOT" 2>/dev/null
    return 0
  fi
  dir=$RXROOT/snap-$label
  echo "dir: $dir"
  if [ -f "$dir/sampler.pid" ]; then
    pid=$(cat "$dir/sampler.pid")
    if kill -0 "$pid" 2>/dev/null; then echo "pid: $pid alive"; else echo "pid: $pid dead"; fi
  fi
  if [ -f "$dir/samples.tsv" ]; then
    echo "samples: $(wc -l <"$dir/samples.tsv") lines"
    tail -3 "$dir/samples.tsv"
  fi
  if [ -f "$dir/crash-first.txt" ]; then echo "CRASH FILE PRESENT:"; cat "$dir/crash-first.txt"; fi
  [ -f "$dir/markers.log" ] && cat "$dir/markers.log"
}

do_collect() {
  label=$1
  dir=$RXROOT/snap-$label
  [ -d "$dir" ] || die "no snapshot dir $dir"
  if [ -f "$dir/sampler.pid" ]; then
    pid=$(cat "$dir/sampler.pid" 2>/dev/null)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      warn "sampler still running - stopping it before collect"
      kill "$pid" 2>/dev/null || true
      sleep 1
    fi
  fi
  members=""
  for m in "snap-$label-pre" "snap-$label-post" "snap-$label"; do
    [ -d "$RXROOT/$m" ] && members="$members $m"
  done
  [ -n "$members" ] || die "no snapshot directory for label $label"
  # word splitting of $members is intentional: it is a list of member names
  tar -czf "$RXROOT/$label.tar.gz" -C "$RXROOT" $members 2>/dev/null || die "tar failed"
  if have md5sum; then dg=$(md5sum "$RXROOT/$label.tar.gz" | cut -d' ' -f1); else dg=NA; fi
  ls -l "$RXROOT/$label.tar.gz"
  echo "BUNDLE: $RXROOT/$label.tar.gz md5=$dg"
}

# ===========================================================================
# selftest - host-safe structural checks (no device access)
# ===========================================================================
do_selftest() {
  rc=0
  ck() {
    if eval "$2" >/dev/null 2>&1; then echo "ok   $1"; else echo "FAIL $1"; rc=1; fi
  }
  # the checks below inspect only the code BEFORE this function, so the
  # pattern strings in the checks cannot match themselves.
  bod=$(mktemp 2>/dev/null || echo /tmp/rx-cap-selftest.$$)
  sed '/^do_selftest() {/,$d' "$0" >"$bod"
  ck "script parses (sh -n)"            'sh -n "$0"'
  ck "declares READ-ONLY contract"      'sed -n "2,30p" "$bod" | grep -q READ-ONLY'
  ck "no /dev/mem write open"           '! grep -q "O_RDWR" "$bod"'
  ck "no of=/dev/mem"                   '! grep -q "of=/dev/mem" "$bod"'
  ck "no module load/unload commands"   '! grep -Eq "(^|[;[:space:]])(rmmod|insmod|modprobe)[[:space:]]" "$bod"'
  ck "no reboot/poweroff command"       '! grep -Eq "(^|[;[:space:]])(reboot|poweroff|halt)[[:space:]]" "$bod"'
  ck "no flash write form"              '! grep -Eq "mtd[[:space:]]+(write|erase)|flash_erase|nandwrite|ubiformat" "$bod"'
  ck "live bank CA present"             'grep -q "CA_LIVE=0x40050000" "$bod"'
  ck "vendor mask constant present"     'grep -q "fffffcc1" "$bod"'
  ck "status bit map complete"          'grep -q "rx_ppdu_desc_error" "$bod" && grep -q "data_ring_overrun" "$bod"'
  ck "bar0 translation documented"      'grep -q "0x3b8000" "$bod" && grep -q "0x6f8008" "$bod"'
  ck "crash exit code is 3"             'grep -q "exit 3" "$bod"'
  ck "raw path is O_RDONLY only"        'grep -q "dd if=/dev/mem" "$bod"'
  rm -f "$bod"
  if [ "$rc" = 0 ]; then echo "SELFTEST PASS"; fi
  exit "$rc"
}

# ===========================================================================
# main
# ===========================================================================
[ $# -ge 1 ] || { usage; exit 1; }
cmd=$1; shift
case "$cmd" in
  --help|-h|help) usage; exit 0 ;;
  __sample) do_run_internal "$@" ;;
  check) find_dev >/dev/null 2>&1 || true; do_check ;;
  snapshot) [ $# -ge 1 ] || die "snapshot needs LABEL"; lab=$1; shift; find_dev || die "no device"; do_snapshot "$lab" "$@" ;;
  run) [ $# -ge 1 ] || die "run needs LABEL"; lab=$1; shift; find_dev || die "no device"; do_run "$lab" "$@" ;;
  stop) [ $# -ge 1 ] || die "stop needs LABEL"; do_stop "$1" ;;
  status) if [ $# -ge 1 ]; then do_status "$1"; else do_status ""; fi ;;
  collect) [ $# -ge 1 ] || die "collect needs LABEL"; do_collect "$1" ;;
  selftest) do_selftest ;;
  *) usage; die "unknown command '$cmd'" ;;
esac
exit 0

