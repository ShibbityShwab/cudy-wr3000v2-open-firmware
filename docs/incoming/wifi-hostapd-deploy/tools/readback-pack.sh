#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# readback-pack.sh - print the read-only BAR0 readback pack for the AP-start
# ladder, derived from recipes/ap_start_ladder.json and cross-checked by
# tools/check-ladder.py.  Host-only: it NEVER contacts the router.  The
# operator runs the printed addresses through the bench tooling
# (wifi-bench-tooling/tools/bar0read.c helper, deployed as
# /root/hi5622-bar0read) or as bench_session.py read_pack steps.
#
# usage: ./tools/readback-pack.sh [--format table|shell]
#   table (default) -> the expected readback table
#   shell           -> one bar0read command per line for the operator
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
FORMAT=table
BAR0READ=${BAR0READ:-/root/hi5622-bar0read}
while [ $# -gt 0 ]; do
	case "$1" in
		--format) shift; FORMAT=$1 ;;
		-h|--help) sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done
if ! python3 "$LANE/tools/check-ladder.py" >/dev/null 2>&1; then
	echo "readback-pack: ladder recipe is inconsistent; run tools/check-ladder.py" >&2
	exit 1
fi
LADDER=$LANE/recipes/ap_start_ladder.json
if [ "$FORMAT" = shell ]; then
	echo "# read-only AP-start readback pack - run on the box."
	echo "# bar0read reads one 32-bit BAR0 word (static ARM /dev/mem mmap, READ-ONLY)."
	echo "# Never write these addresses; mask/staging must stay 0."
	python3 - "$LADDER" "$BAR0READ" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
bar0read = sys.argv[2]
for r in d["readback_pack"]:
    if "bar0" in r:
        print("%s %s   # %s -> %s" % (bar0read, r["bar0"], r["name"], r["expect"]))
PY
	exit 0
fi
printf '%-48s %-12s %s\n' "readback" "BAR0" "expected"
printf '%-48s %-12s %s\n' "--------" "----" "--------"
python3 - "$LADDER" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
for r in d["readback_pack"]:
    where = r.get("bar0") or ("expr: " + str(r.get("expr")))
    print("%-48s %-12s %s" % (r["name"][:48], where.split("expr: ")[0], r["expect"]))
PY
echo
echo "# read-only only: never write these addresses.  mask/staging must stay 0;"
echo "# a nonzero mask or staging word is a STOP, not a retry."