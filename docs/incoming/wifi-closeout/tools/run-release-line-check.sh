#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# run-release-line-check.sh - host-only wrapper around the upstream series
# release-line validator.  It needs a pristine linux-6.18.60 tarball (the
# OpenWrt download dirs on this host carry one); without it the check is
# SKIPPED (exit 77) rather than failed, because the tarball is a local
# OpenWrt build input, not a repository artifact.
set -u
SERIES=$(cd "$(dirname "$0")/../../../../upstream/series-6.18" && pwd)
TARBALL=""
for candidate in /home/*/openwrt-lab/dl/linux-6.18.60.tar.xz; do
  [ -f "$candidate" ] && TARBALL="$candidate" && break
done
if [ -z "$TARBALL" ]; then
  echo "SKIP: no linux-6.18.60.tar.xz under /home/*/openwrt-lab/dl (validator needs the pristine tarball)"
  exit 77
fi
echo "using tarball: $TARBALL"
cd "$SERIES"
exec bash tools/check-release-line.sh "$TARBALL" /tmp/wifi-closeout-release-line
