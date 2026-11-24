#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# check-artifacts.sh - host-only artifact pin check for the wifi-bench-session.
#
# Reads ../artifacts.env (or $WB_ARTIFACTS) and verifies every pin the selected
# phases need: file exists, size matches, sha256 matches (or one of the
# documented ACCEPT pairs).  Never contacts the router, never writes outside
# the lane.  Exit: 0 all checked pins OK; 1 at least one pin failed; 2 usage.
# 'pending' pins are reported DEPENDENCY-PENDING (exit 1 only when --strict).
#
# usage: tools/check-artifacts.sh [--strict] [phase ...]   phases: s a x (default: s a x)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$LANE/../../../.." && pwd)
ENV_FILE=${WB_ARTIFACTS:-$LANE/artifacts.env}
STRICT=0
PHASES=""
for a in "$@"; do
  case "$a" in
    --strict) STRICT=1 ;;
    -h|--help) sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    s|a|x) PHASES="$PHASES $a" ;;
    *) echo "check-artifacts: unknown argument: $a" >&2; exit 2 ;;
  esac
done
[ -n "$PHASES" ] || PHASES=" s a x"
[ -f "$ENV_FILE" ] || { echo "check-artifacts: missing $ENV_FILE" >&2; exit 2; }
# shellcheck disable=SC1090
. "$ENV_FILE"
say() { printf '%s\n' "$*"; }
warn=0
pend=0

sha_of() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }
size_of() { stat -c %s "$1" 2>/dev/null; }

check_one() { # label path want_sha want_size accept_list(optional)
  label=$1; path=$2; want_sha=$3; want_size=$4; accept=${5:-}
  case "$path" in
    pending) : ;;
    /*) : ;;
    *) path="$ROOT/$path" ;;
  esac
  if [ "$path" = "pending" ] || [ "$want_sha" = "pending" ]; then
    say "$(printf '%-22s' "$label") DEPENDENCY-PENDING (lane pin not delivered)"
    pend=1; return 0
  fi
  if [ ! -f "$path" ]; then
    say "$(printf '%-22s' "$label") FAIL missing $path"; warn=1; return 1
  fi
  got_sha=$(sha_of "$path"); got_size=$(size_of "$path")
  ok=0
  if [ -n "$accept" ]; then
    for pair in $accept; do
      a_sha=${pair%%:*}; a_size=${pair##*:}
      if [ "$got_sha" = "$a_sha" ]; then
        if [ -n "$a_size" ] && [ "$a_size" != "$want_size" ] && [ "$got_size" != "$a_size" ]; then :; fi
        ok=1
      fi
    done
  fi
  if [ "$got_sha" = "$want_sha" ]; then ok=1; fi
  if [ "$ok" -ne 1 ]; then
    say "$(printf '%-22s' "$label") FAIL sha256=$got_sha size=$got_size (want $want_sha $want_size)"; warn=1; return 1
  fi
  if [ -n "$want_size" ] && [ "$want_size" != "pending" ] && [ "$got_size" != "$want_size" ]; then
    # an accepted alternate records its own size in ACCEPT; allow only if it is
    # the documented alternate (sha matched an ACCEPT entry)
    alt=0
    for pair in ${accept:-}; do
      a_sha=${pair%%:*}; a_size=${pair##*:}
      [ "$got_sha" = "$a_sha" ] && [ "$got_size" = "$a_size" ] && alt=1
    done
    if [ "$alt" -ne 1 ]; then
      say "$(printf '%-22s' "$label") FAIL size=$got_size (want $want_size)"; warn=1; return 1
    fi
  fi
  say "$(printf '%-22s' "$label") OK sha256=${got_sha%??????????????????????????????????????????????????????}... size=$got_size"
  return 0
}

check_onbox() { # label sha remote - installed by a dependency lane's attended deploy
  label=$1; sha=$2; remote=$3
  if [ "$sha" = "pending" ] || [ -z "$sha" ]; then
    say "$(printf '%-22s' "$label") DEPENDENCY-PENDING (lane pin not delivered)"
    pend=1; return 0
  fi
  say "$(printf '%-22s' "$label") OK on-box=$remote sha256=${sha%??????????????????????????????????????????????????????}... (verified in phase A)"
  return 0
}

say "check-artifacts: env=$ENV_FILE phases:$(printf ' %s' $PHASES)"
case " $PHASES " in
  *" s "*) check_one "FW PD4" "$FW_PD4" "$FW_PD4_SHA" "$FW_PD4_SIZE"
           check_one "FW manifest" "$FW_MANIFEST" "$FW_MANIFEST_SHA" ""
           check_one "PHY block" "$WIFI_BLOCK" "$WIFI_BLOCK_SHA" "$WIFI_BLOCK_SIZE"
           check_one "cfg80211.ko" "$CFG80211_KO" "$CFG80211_KO_SHA" "$CFG80211_KO_SIZE"
           check_one "iw" "$IW_BIN" "$IW_BIN_SHA" ""
           check_one "libnl-tiny" "$LIBNL_TINY" "$LIBNL_TINY_SHA" ""
           check_one "scan module" "$SCAN_MODULE" "$SCAN_MODULE_SHA" "$SCAN_MODULE_SIZE" "$SCAN_MODULE_ACCEPT" ;;
esac
case " $PHASES " in
  *" a "*) check_one "FW PD4" "$FW_PD4" "$FW_PD4_SHA" "$FW_PD4_SIZE"
           check_one "FW manifest" "$FW_MANIFEST" "$FW_MANIFEST_SHA" ""
           check_one "PHY block" "$WIFI_BLOCK" "$WIFI_BLOCK_SHA" "$WIFI_BLOCK_SIZE"
           check_one "cfg80211.ko" "$CFG80211_KO" "$CFG80211_KO_SHA" "$CFG80211_KO_SIZE"
           check_one "iw" "$IW_BIN" "$IW_BIN_SHA" ""
           check_one "libnl-tiny" "$LIBNL_TINY" "$LIBNL_TINY_SHA" ""
           check_one "AP module" "$AP_MODULE" "$AP_MODULE_SHA" "$AP_MODULE_SIZE"
           if [ "${HOSTAPD_MODE:-apk}" = "apk" ]; then
             check_onbox "hostapd on-box" "${HOSTAPD_ONBOX_SHA:-pending}" "${HOSTAPD_REMOTE_BIN:-/usr/sbin/hostapd}"
             check_onbox "hostapd conf" "${HOSTAPD_CONF_SHA:-pending}" "${HOSTAPD_REMOTE_CONF:-/root/hostapd-deploy/hostapd-open.conf}"
           else
             check_one "hostapd bin" "$HOSTAPD_BIN" "$HOSTAPD_BIN_SHA" ""
           fi ;;
esac
case " $PHASES " in
  *" x "*) check_one "FW PD4" "$FW_PD4" "$FW_PD4_SHA" "$FW_PD4_SIZE"
           check_one "FW manifest" "$FW_MANIFEST" "$FW_MANIFEST_SHA" ""
           check_one "PHY block" "$WIFI_BLOCK" "$WIFI_BLOCK_SHA" "$WIFI_BLOCK_SIZE"
           check_one "cfg80211.ko" "$CFG80211_KO" "$CFG80211_KO_SHA" "$CFG80211_KO_SIZE"
           check_one "iw" "$IW_BIN" "$IW_BIN_SHA" ""
           check_one "libnl-tiny" "$LIBNL_TINY" "$LIBNL_TINY_SHA" ""
           check_one "X merged module" "$X_MODULE" "$X_MODULE_SHA" "$X_MODULE_SIZE"
           check_one "X sequence" "$X_SEQUENCE" "$X_SEQUENCE_SHA" "" ;;
esac
if [ "$warn" -ne 0 ]; then
  say "check-artifacts: FAIL ($warn pin(s) failed)"; exit 1
fi
if [ "$pend" -ne 0 ] && [ "$STRICT" -eq 1 ]; then
  say "check-artifacts: DEPENDENCY-PENDING (--strict)"; exit 1
fi
say "check-artifacts: OK$( [ "$pend" -ne 0 ] && printf ' (some phases DEPENDENCY-PENDING)')"
exit 0