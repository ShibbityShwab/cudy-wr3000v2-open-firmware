#!/bin/sh
# SPDX-License-Identifier: GPL-2.0 OR MIT
# appie2 host KAT runner - builds and runs the clean-room call-order replica.
# Host-only: no kernel tree, no modules, no device, no network.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc=${CC:-cc}
out=${TMPDIR:-/tmp}/appie2-selftest
"$cc" -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
	-I"$here/src" \
	"$here/src/appie2_selftest.c" "$here/src/appie2_replica.c" \
	-o "$out"
exec "$out"
