#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# ap-readback.sh - PHASE A read-only firmware readback pack (task-127)
#
# Read-only companion of ap-fire.sh.  The host driver runs it after the AP
# stage (pass or stop) to capture the firmware VAP-context evidence for the
# offline analysis: PD4 handshake page, boot state, exception mask/staging,
# mailbox out[0]/out[2], module-0 ctx pointer chain, the [I] vap0+0x10
# (beacon interval) / +0x14 (DTIM) dereference, provider/H2D counters and the
# AP-relevant dmesg lines.  It NEVER writes, moves no ring pointer, touches no
# mailbox and does not stop hostapd.
#
# usage: sh ap-readback.sh
set -u
RDIR=${WB_RDIR:-/root/hostapd-deploy/evidence}
BAR0READ=${WB_BAR0READ:-/root/hi5622-bar0read}
EP=${WB_ENDPOINT:-0000:00:00.0}
mkdir -p "$RDIR" || exit 11
R="$RDIR/ap-readback-report.txt"
: > "$R"
say() { printf '%s\n' "$*" >> "$R"; printf 'DL %s\n' "$*"; }
stop() { say "STOP:$1: $2"; say "SESSION_RESULT: STOP:$1"; exit 10; }
say "APREADBACK begin=$(date +%s) kernel=$(uname -r)"
P="/sys/bus/pci/devices/$EP"
[ -d "$P" ] || stop endpoint-missing "no PCI endpoint $EP"
[ -x "$BAR0READ" ] || stop tool-missing "bar0read helper missing: $BAR0READ"
num() {
  v=$(printf '%s' "$1" | tr -d ' \t\n')
  case "$v" in "") printf 'invalid'; return;; esac
  case "$v" in *[!0-9a-fA-Fx]*) printf 'invalid'; return;; esac
  case "$v" in 0x*|0X*) : ;; *) v="0x$v";; esac
  printf '%d' "$((v))" 2>/dev/null || printf 'invalid'
}
pack() { # tag
  tag=$1
  out=$("$BAR0READ" 0x877000 0x877004 0x877008 0x87700c 0x7d0b60 0x6f8008 0x6f800c 0x3f1010 0x3f12d4 2>>"$R")
  [ $? -eq 0 ] || stop read-pack "bar0read failed"
  set -- $out
  [ $# -eq 9 ] || stop read-pack "expected 9 words, got $#"
  say "READPACK $tag page=$1 $2 $3 $4 state=$5 mask=$6 staging=$7 out0=$8 out2=$9"
}
pack readback
# ctx0 CA = u32(G+0x630) = 0x00119758 on the validated boots; BAR0 = CA +
# 0x6B8000 = 0x7D1758 (wifi-hostapd-deploy readback pack; older 0x7D2758 notes
# are off by 0x1000).  Read the slot first, then the derived addresses.
CTXBAR=0; CTX190BAR=""
SLOT0=$("$BAR0READ" 0x829458 2>/dev/null) || SLOT0=""
if [ -n "$SLOT0" ]; then
  say "READ32 label=module_table.slot0 bar0=0x829458 value=$SLOT0"
  CTXCA=$(num "$SLOT0"); CTXBAR=$((CTXCA + 0x6b8000)); CTX190BAR=$((CTXBAR + 0x190))
  say "READBACK ctx0.ca=0x$(printf %x "$CTXCA") bar0=0x$(printf %x "$CTXBAR") ctx0+0x190.bar0=0x$(printf %x "$CTX190BAR")"
  for off in "$CTXBAR" "$CTX190BAR"; do
    v=$("$BAR0READ" "$off" 2>>"$R") && say "READ32 label=ctx.$off bar0=0x$(printf %x "$off") value=$v" || say "READ32 label=ctx.$off read-failed"
  done
else
  say "READ32 label=module_table.slot0 read-failed"
fi
CTX190=$("$BAR0READ" "$CTX190BAR" 2>/dev/null) || CTX190=""
if [ -n "${CTX190:-}" ]; then
  CA=$(num "$CTX190")
  BAR=""
  if [ "$CA" -ge 0 ] 2>/dev/null && [ "$CA" -le $((0x21ffff)) ]; then BAR=$((CA + 0x6b8000));
  elif [ "$CA" -ge $((0x01200000)) ] && [ "$CA" -le $((0x0137ffff)) ]; then BAR=$((CA - 0xb48000));
  elif [ "$CA" -ge $((0x01000000)) ] && [ "$CA" -le $((0x011dffff)) ]; then BAR=$((CA - 0xe28000));
  elif [ "$CA" -ge $((0x02000000)) ] && [ "$CA" -le $((0x021dffff)) ]; then BAR=$((CA - 0x1b28000));
  elif [ "$CA" -ge $((0x00400000)) ] && [ "$CA" -le $((0x00417fff)) ]; then BAR=$((CA - 0x240000));
  fi
  if [ -n "$BAR" ]; then
    say "READBACK vap0 ca=0x$(printf %x "$CA") bar0=0x$(printf %x "$BAR") convention=resolved"
    for off in 0x10 0x14 0x18c 0x18d 0x64 0x13a; do
      a=$((BAR + off))
      v=$("$BAR0READ" "$a" 2>>"$R") && say "READ32 label=vap+$off bar0=0x$(printf %x "$a") value=$v" || say "READ32 label=vap+$off bar0=0x$(printf %x "$a") read-failed"
    done
  else
    say "READBACK vap0 ca=0x$(printf %x "$CA") convention=unresolved"
  fi
else
  say "READBACK vap0 ctx0+0x190 unreadable"
fi
if [ "$CTXBAR" -gt 0 ]; then
  C18C=$((CTXBAR + 0x18c))
  v=$("$BAR0READ" "$C18C" 2>/dev/null) && say "READ32 label=ctx.aligned_0x18c bar0=0x$(printf %x "$C18C") value=$v" || say "READ32 label=ctx.aligned_0x18c read-failed"
fi
for a in cfg80211_status h2d_status; do
  if [ -r "$P/$a" ]; then cat "$P/$a" >> "$R" 2>&1; else say "READ-FAIL attr=$a"; fi
done
{ dmesg 2>/dev/null | grep -E "start_ap sequence|frame post '(ap_|vap_|user_|scan_req)|VAP .* added|scan DONE" | tail -n 60 >> "$R" 2>&1; } || true
{ iw dev 2>/dev/null >> "$R" 2>&1; } || true
{ iw dev wlan0 info 2>/dev/null >> "$R" 2>&1; } || true
PIDF=${WB_HOSTAPD_PIDFILE:-/root/hostapd-deploy/hostapd.pid}
if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF" 2>/dev/null)" 2>/dev/null; then
  say "READBACK hostapd=alive pid=$(cat "$PIDF" 2>/dev/null)"
else
  say "READBACK hostapd=not-running"
fi
say "SESSION_RESULT: READ-ONLY-PACK"