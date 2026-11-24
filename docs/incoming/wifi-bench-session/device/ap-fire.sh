#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# ap-fire.sh - PHASE A device-side AP-start stage (wifi-bench-session, task-127)
#
# Runs on the bench box AFTER the host driver's cfg80211 base session
# (sequences/session-ap-base.json -> bench_session.py --mode cfg80211) has
# booted the PD4 firmware, loaded cfg80211.ko + the wifi-ap-start provider
# module, reached READY and fired ctx_create + txq_sync72.
#
# Canonical AP-start flow (hostapd-img task-125 facts, 2026-11-26):
#   1. iface must be created as type __ap ("type AP" is rejected by iw 6.17;
#      hostapd v2.12 always sends NL80211_CMD_SET_INTERFACE(AP) and our
#      change_virtual_intf refuses a type change, so wlan0 must already be AP)
#   2. mode-1 VAP base: "iw dev wlan0 scan freq 2412" - the provider posts
#      vap_add g0 id1 48 B + vap_start g0 id1 40 B + scan_req g0 id0xC 520 B
#      ONLY inside hi5622_scan_op_scan when vap_start_first=1 (2026-11-26
#      14:00 fire; the ap-start ladder V1 "iface add posts 0x64/0x65" is wrong)
#   3. hostapd -B -i wlan0 <open conf> -> cfg80211 start_ap -> provider posts
#      the carrier's AP sequence, each frame through the inherited
#      hi5622_frame_post gate (allow_frames=16 one-shot budget):
#        ap_start_mode=0 (g0 id1 static, default): 2 frames - ap_ssid
#          (key 0x0005, 64 B) then ap_cfg (key 0x0127, 184 B).  The static
#          firmware table A has no 0x006D (auth) row and the g0 carrier omits
#          the VAP start (the mode-1 VAP base already started it).
#        ap_start_mode=1 (WAL g2 vendor order): 4 frames - ap_ssid, ap_auth
#          (0x006D), ap_cfg (0x0127), ap_vap_start (0x0065).
#   4. read-only firmware readback pack (vap0+0x10 beacon interval, +0x14 DTIM)
#
# FAIL-CLOSED: ordered writes/gates; the first failure emits STOP:<step> and
# exits 10. NEVER retried in the same boot (the host driver power-cycles).
# No rmmod, no UART, no flash, no CA 0x400392F0 / RC-misc access, no mailbox
# id 5, no group-1/4 frame; device writes are nl80211/hostapd ops only.
#
# usage: sh ap-fire.sh        (WB_* env overrides below)
set -u

RDIR=${WB_RDIR:-/root/hostapd-deploy/evidence}
HOSTAPD=${WB_HOSTAPD:-/root/hostapd-deploy/hostapd}
CONF=${WB_HOSTAPD_CONF:-/root/hostapd-deploy/hostapd-open.conf}
PIDF=${WB_HOSTAPD_PIDFILE:-/root/hostapd-deploy/hostapd.pid}
BAR0READ=${WB_BAR0READ:-/root/hi5622-bar0read}
VAPBASE_TIMEOUT=${WB_VAPBASE_TIMEOUT:-30}
HOSTAPD_WAIT=${WB_HOSTAPD_WAIT:-30}
EP=${WB_ENDPOINT:-0000:00:00.0}
AP_START_MODE=${WB_AP_START_MODE:-0}

mkdir -p "$RDIR" || exit 11
R="$RDIR/ap-fire-report.txt"
: > "$R"
say()  { printf '%s\n' "$*" >> "$R"; printf 'DL %s\n' "$*"; }
stop() { say "STOP:$1: $2"; say "GATE-FAIL name=$1"; say "SESSION_RESULT: STOP:$1"; printf 'ap-fire STOP %s: %s (report %s)\n' "$1" "$2" "$R" >&2; exit 10; }
pass() { say "SESSION_RESULT: $1"; exit 0; }

say "APFIRE begin=$(date +%s) kernel=$(uname -r)"
[ -d "/sys/bus/pci/devices/$EP" ] || stop endpoint-missing "no PCI endpoint $EP"
P="/sys/bus/pci/devices/$EP"
[ -r "$P/resource0" ] || stop resource-missing "$P/resource0 not readable"
[ -x "$BAR0READ" ] || stop tool-missing "bar0read helper missing: $BAR0READ"
[ -x "$HOSTAPD" ] || stop tool-missing "hostapd binary missing: $HOSTAPD (wifi-hostapd-deploy lane pin)"
[ -r "$CONF" ] || stop config-missing "hostapd config missing: $CONF"
grep -q '^hi5622_wifi ' /proc/modules 2>/dev/null || stop module-missing "hi5622_wifi not loaded"
{ grep -q '^cfg80211 ' /proc/modules 2>/dev/null || [ -d /sys/module/cfg80211 ]; } || stop module-missing "cfg80211 not loaded"
[ -d /sys/class/ieee80211/phy0 ] || stop wiphy-missing "no /sys/class/ieee80211/phy0"
if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF" 2>/dev/null)" 2>/dev/null; then
  stop hostapd-already-running "hostapd pid $PIDF already alive"
fi
IW=""
for c in /usr/sbin/iw /sbin/iw /tmp/hi5622-bench-iw; do
  if [ -x "$c" ]; then IW="$c"; break; fi
done
[ -n "$IW" ] || stop tool-missing "no iw binary (image iw or /tmp/hi5622-bench-iw)"
say "APFIRE tools iw=$IW hostapd=$HOSTAPD bar0read=$BAR0READ"

# numeric normalisation: 0x.... / decimal / empty -> decimal value (or "invalid")
num() {
  v=$(printf '%s' "$1" | tr -d ' \t\n')
  case "$v" in "") printf 'invalid'; return;; esac
  case "$v" in *[!0-9a-fA-Fx]*) printf 'invalid'; return;; esac
  case "$v" in 0x*|0X*) : ;; *) v="0x$v";; esac
  printf '%d' "$((v))" 2>/dev/null || printf 'invalid'
}
gate_eq() { # name got want(hex-or-dec)
  g=$(num "$2"); w=$(num "$3")
  [ "$g" = "$w" ] && say "GATE $1 PASS ($2 == $3)" || stop "$1" "got $2 want $3"
}
gate_zero() { # name got
  g=$(num "$2"); [ "$g" = "0" ] && say "GATE $1 PASS ($2)" || stop "$1" "expected 0, got $2"
}
gate_nonzero() { # name got
  g=$(num "$2"); [ "$g" != "0" ] && [ "$g" != "invalid" ] && say "GATE $1 PASS ($2)" || stop "$1" "expected nonzero, got $2"
}

# ---- read packs (read-only; the D2/E1 hardware-validated BAR0 offsets) -----
RP_P0=; RP_P4=; RP_P8=; RP_PC=; RP_STATE=; RP_MASK=; RP_STG=; RP_OUT0=; RP_OUT2=
read_pack() { # tag
  tag=$1
  out=$("$BAR0READ" 0x877000 0x877004 0x877008 0x87700c 0x7d0b60 0x6f8008 0x6f800c 0x3f1010 0x3f12d4 2>>"$R")
  rc=$?
  [ "$rc" -eq 0 ] || stop read-pack "bar0read rc=$rc"
  set -- $out
  [ $# -eq 9 ] || stop read-pack "expected 9 words, got $#"
  RP_P0=$1; RP_P4=$2; RP_P8=$3; RP_PC=$4; RP_STATE=$5; RP_MASK=$6; RP_STG=$7; RP_OUT0=$8; RP_OUT2=$9
  say "READPACK $tag page=$RP_P0 $RP_P4 $RP_P8 $RP_PC state=$RP_STATE mask=$RP_MASK staging=$RP_STG out0=$RP_OUT0 out2=$RP_OUT2"
}
read_pack pre
gate_eq ap.page0-pre "$RP_P0" 0x86807fef
gate_eq ap.page4-pre "$RP_P4" 0x00000007
gate_eq ap.page8-pre "$RP_P8" 0x00000002
gate_eq ap.pagec-pre "$RP_PC" 0x0000abcd
gate_eq ap.state-pre "$RP_STATE" 0x00000001
gate_zero ap.mask-pre "$RP_MASK"
gate_zero ap.staging-pre "$RP_STG"
gate_zero ap.out0-pre "$RP_OUT0"
gate_zero ap.out2-pre "$RP_OUT2"

# ---- interface as __ap + link up ------------------------------------------
say "SHELL iface-add $IW phy phy0 interface add wlan0 type __ap"
if ! "$IW" phy phy0 interface add wlan0 type __ap >>"$R" 2>&1; then
  stop iface-add "iw iface add type __ap failed"
fi
"$IW" dev 2>/dev/null | grep -q "Interface wlan0" || stop iface-add "wlan0 not listed by iw dev"
say "GATE iface-add PASS"
"$IW" dev wlan0 info >>"$R" 2>&1 || true
say "SHELL link-up ip link set wlan0 up"
if ! ip link set wlan0 up >>"$R" 2>&1; then
  stop link-up "ip link set wlan0 up failed"
fi
say "GATE link-up PASS"

# ---- mode-1 VAP base: ONE one-channel scan --------------------------------
if command -v timeout >/dev/null 2>&1; then
  timeout "$VAPBASE_TIMEOUT" "$IW" dev wlan0 scan freq 2412 > "$RDIR/vapbase-scan.out" 2>&1
else
  "$IW" dev wlan0 scan freq 2412 > "$RDIR/vapbase-scan.out" 2>&1
fi
VSRC=$?
say "SHELL vapbase-scan rc=$VSRC (output $RDIR/vapbase-scan.out)"
case "$VSRC" in
  0|124) : ;;
  *) stop vapbase-scan "iw dev wlan0 scan freq 2412 rc=$VSRC (scan refused)" ;;
esac
cat "$RDIR/vapbase-scan.out" >> "$R" 2>/dev/null || true
i=0
while [ "$i" -lt 20 ]; do
  if dmesg 2>/dev/null | grep -q "frame post 'vap_add'" && dmesg 2>/dev/null | grep -q "frame post 'vap_start'"; then break; fi
  i=$((i + 1)); sleep 1
done
dmesg 2>/dev/null | grep -E "frame post '(vap_add|vap_start|scan_req)'|VAP .* added|scan DONE" | tail -n 20 >> "$R" 2>&1 || true
dmesg 2>/dev/null | grep -q "frame post 'vap_add'" || stop vapbase-vap_add "no firmware VAP add frame post"
dmesg 2>/dev/null | grep -q "frame post 'vap_start'" || stop vapbase-vap_start "no firmware VAP start frame post"
say "GATE vapbase.vap_add PASS"
say "GATE vapbase.vap_start PASS"

# ---- module-table slot0 + VAP pointer chain (module-0 context) ------------
# The radio object's module-table slot 0 (G+0x630) is boot-static 0x00119758
# on the validated PD4 boots (hardware 2026-11-26 12:00/13:00 fires; do not
# dereference a guessed context if it drifted).
if SLOT0=$("$BAR0READ" 0x829458 2>>"$R"); then
  say "READBACK module-table.slot0 bar0=0x829458 value=$SLOT0"
  gate_eq ap.slot0 "$SLOT0" 0x00119758
else
  say "READBACK module-table.slot0 read-failed"
fi
# ctx0 CA = u32(G+0x630) = 0x00119758 on the validated PD4 boots; the CPU-view
# -> BAR0 convention is CA + 0x6B8000 => 0x7D1758 (mapped by the wifi-hostapd-
# deploy lane's readback pack: ctx4 cross-check 0x11C458 -> 0x7D4458, and the
# D2 sequence's ctx4+0x190 at BAR0 0x7D45E8).  Older notes claiming 0x7D2758
# are off by 0x1000 - do not use them.
CTXCA=$(num "$SLOT0")
CTXBAR=$((CTXCA + 0x6b8000))
CTX190BAR=$((CTXBAR + 0x190))
if CTX0=$("$BAR0READ" "$CTXBAR" 2>>"$R") && CTX190=$("$BAR0READ" "$CTX190BAR" 2>>"$R"); then
  say "READBACK ctx0.ca=0x$(printf %x "$CTXCA") bar0=0x$(printf %x "$CTXBAR") value=$CTX0"
  say "READBACK ctx0+0x190.bar0=0x$(printf %x "$CTX190BAR") value=$CTX190"
  gate_nonzero ap.vap-pointer "$CTX190"
  VAPCA=$(num "$CTX190")
  VAPBAR=""
  resolve_ca() { # ca -> BAR0 offset, project pointer conventions
    ca=$1
    if [ "$ca" -ge 0 ] && [ "$ca" -le $((0x21ffff)) ]; then printf '%d' $((ca + 0x6b8000)); return 0; fi
    if [ "$ca" -ge $((0x01200000)) ] && [ "$ca" -le $((0x0137ffff)) ]; then printf '%d' $((ca - 0xb48000)); return 0; fi
    if [ "$ca" -ge $((0x01000000)) ] && [ "$ca" -le $((0x011dffff)) ]; then printf '%d' $((ca - 0xe28000)); return 0; fi
    if [ "$ca" -ge $((0x02000000)) ] && [ "$ca" -le $((0x021dffff)) ]; then printf '%d' $((ca - 0x1b28000)); return 0; fi
    if [ "$ca" -ge $((0x00400000)) ] && [ "$ca" -le $((0x00417fff)) ]; then printf '%d' $((ca - 0x240000)); return 0; fi
    return 1
  }
  if VAPBAR=$(resolve_ca "$VAPCA"); then
    say "READBACK vap0 ca=0x$(printf %x "$VAPCA") bar0=0x$(printf %x "$VAPBAR") convention=resolved"
    VAP10=""; VAP14=""; VAP64=""
    if v=$("$BAR0READ" $((VAPBAR + 0x10)) 2>/dev/null); then VAP10=$v; fi
    if v=$("$BAR0READ" $((VAPBAR + 0x14)) 2>/dev/null); then VAP14=$v; fi
    if v=$("$BAR0READ" $((VAPBAR + 0x64)) 2>/dev/null); then VAP64=$v; fi
    say "READBACK vap0+0x10=${VAP10:-<read-failed>} vap0+0x14=${VAP14:-<read-failed>} vap0+0x64=${VAP64:-<read-failed>}"
  else
    say "READBACK vap0 ca=0x$(printf %x "$VAPCA") convention=unresolved"
  fi
  C18C_PRE=$("$BAR0READ" $((CTXBAR + 0x18c)) 2>/dev/null) || C18C_PRE=""
  say "READBACK ctx+0x18c/0x18d.pre bar0=0x$(printf %x $((CTXBAR + 0x18c))) aligned-word=${C18C_PRE:-<read-failed>}"
else
  say "READBACK ctx-chain read-failed (read-only evidence continues); CTXBAR=0"
  CTXBAR=0
fi

# ---- hostapd NEW_BEACON -> driver start_ap -> provider AP sequence ---------
say "SHELL hostapd $HOSTAPD -B -i wlan0 -P $PIDF $CONF"
"$HOSTAPD" -B -i wlan0 -P "$PIDF" "$CONF" > "$RDIR/hostapd.out" 2>&1
HSRC=$?
say "SHELL hostapd rc=$HSRC"
tail -n 40 "$RDIR/hostapd.out" >> "$R" 2>/dev/null || true
[ "$HSRC" -eq 0 ] || stop hostapd-rc "hostapd -B rc=$HSRC (start_ap did not return 0; see $RDIR/hostapd.out)"
i=0
while [ "$i" -lt "$HOSTAPD_WAIT" ]; do
  if dmesg 2>/dev/null | grep -q "start_ap sequence posted to provider"; then break; fi
  i=$((i + 1)); sleep 1
done
dmesg 2>/dev/null | grep -E "start_ap sequence|VAP .* added|frame post 'ap_" | tail -n 30 >> "$R" 2>&1 || true
dmesg 2>/dev/null | grep -q "not posted" && stop ap-frame-refused "a start_ap sequence frame was not posted"
case "$AP_START_MODE" in
  0) AP_FRAMES="ap_ssid ap_cfg" ;;
  1) AP_FRAMES="ap_ssid ap_auth ap_cfg ap_vap_start" ;;
  *) stop ap-start-mode "unsupported ap_start_mode=$AP_START_MODE (want 0/1)" ;;
esac
say "APFIRE ap_start_mode=$AP_START_MODE expected_frames=$AP_FRAMES"
for lbl in $AP_FRAMES; do
  dmesg 2>/dev/null | grep -q "frame post '$lbl'" || stop "ap-frame-$lbl" "no frame post '$lbl' in dmesg"
  say "GATE ap-frame.$lbl PASS"
done
dmesg 2>/dev/null | grep -q "start_ap sequence posted to provider" || stop ap-sequence "no 'start_ap sequence posted to provider' dmesg line"
say "GATE ap-sequence PASS"
dmesg 2>/dev/null | grep -q "VAP 'wlan0' added" && say "GATE ap-vap-added PASS" || say "WARN ap-vap-added dmesg line not matched"

# ---- provider/H2D counters + post-health packs ----------------------------
cat "$P/cfg80211_status" >> "$R" 2>&1 || say "READ-FAIL cfg80211_status"
cat "$P/h2d_status" >> "$R" 2>&1 || say "READ-FAIL h2d_status"
CFG=$(cat "$P/cfg80211_status" 2>/dev/null || true)
case "$CFG" in *"refused=0"*) say "GATE ap.provider-refused PASS";; *) stop ap-provider-refused "cfg80211_status refused!=0/unreadable";; esac
case "$CFG" in *"timeout=0"*) say "GATE ap.provider-timeout PASS";; *) stop ap-provider-timeout "cfg80211_status timeout!=0";; esac
case "$CFG" in *"bad=0"*) say "GATE ap.provider-bad PASS";; *) stop ap-provider-bad "cfg80211_status bad!=0";; esac
read_pack post
gate_eq ap.page0-post "$RP_P0" 0x86807fef
gate_eq ap.page4-post "$RP_P4" 0x00000007
gate_eq ap.page8-post "$RP_P8" 0x00000002
gate_eq ap.pagec-post "$RP_PC" 0x0000abcd
gate_eq ap.state-post "$RP_STATE" 0x00000001
gate_zero ap.mask-post "$RP_MASK"
gate_zero ap.staging-post "$RP_STG"
gate_zero ap.out0-post "$RP_OUT0"
gate_zero ap.out2-post "$RP_OUT2"
C18C_POST=$("$BAR0READ" $((CTXBAR + 0x18c)) 2>/dev/null) || C18C_POST=""
say "READBACK ctx+0x18c/0x18d.post bar0=0x$(printf %x $((CTXBAR + 0x18c))) aligned-word=${C18C_POST:-<read-failed>}"
if [ -n "${C18C_PRE:-}" ] && [ -n "${C18C_POST:-}" ] && [ "$C18C_PRE" != "$C18C_POST" ]; then
  say "GATE ap.ctx18c-change PASS ($C18C_PRE -> $C18C_POST)"
else
  say "WARN ap.ctx18c-change unchanged/unreadable"
fi

# ---- verdict --------------------------------------------------------------
if [ -n "${VAPBAR:-}" ] && [ -n "${VAP10:-}" ] && [ -n "${VAP14:-}" ]; then
  if [ "$(num "$VAP10")" = "$(num 100)" ] && [ "$(num "$VAP14")" = "$(num 2)" ]; then
    say "GATE ap.readback PASS (beacon_interval=100 dtim=2)"
    say "AP-STAGE END state=AP-CONFIRM"
    pass PASS
  fi
  say "AP-STAGE END state=AP-READBACK-MISMATCH"
  stop ap-readback "vap0+0x10/+0x14 want 100/2, got $VAP10/$VAP14"
fi
say "AP-STAGE END state=AP-ACCEPTED-READBACK-UNRESOLVED (frames consumed + clean packs; vap0+0x10/+0x14 deref unresolved)"
pass PASS-PARTIAL