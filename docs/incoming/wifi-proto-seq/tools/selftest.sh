#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# selftest.sh - run the wifi-proto-seq lane's host-only checks (no device contact).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../../.." && pwd)
PY="$repo/.provision-venv/bin/python"
[ -x "$PY" ] || PY=python3
echo "== proto_seq_frames =="
"$PY" "$here/proto_seq_frames.py" --selftest
echo "== fw_proto_static =="
"$PY" "$here/fw_proto_static.py" --selftest
echo "== alg_cmd_table =="
"$PY" "$here/alg_cmd_table.py" --selftest
echo "== recipes JSON =="
"$PY" - "$here/../recipes" <<'PYEOF'
import json, os, sys
d = sys.argv[1]
names = sorted(os.listdir(d))
for n in names:
    if n.endswith(".json"):
        with open(os.path.join(d, n)) as fh:
            json.load(fh)
        print("JSON OK:", n)
PYEOF
echo "all wifi-proto-seq host checks PASS"
