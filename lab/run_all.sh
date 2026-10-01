#!/bin/sh
set -eu
LABEL=${1:?usage: run_all.sh <label> <FIRMWARE.bin> <wifi.ko> [outdir]}
FW=${2:?FIRMWARE.bin path}
KO=${3:?wifi .ko path}
OUT=${4:-out/$LABEL}
PY=${PY:-python3}

mkdir -p "$OUT"
echo "lab run: $LABEL -> $OUT"

for tool in fw_tables fw_dispatch fw_symbols fw_callgraph; do
	[ -f "$(dirname "$0")/$tool.py" ] || continue
	case "$tool" in
		fw_tables) "$PY" "$(dirname "$0")/$tool.py" "$FW" "$OUT/$tool.csv" > "$OUT/$tool.log" 2>&1 || true ;;
		*) "$PY" "$(dirname "$0")/$tool.py" "$FW" > "$OUT/$tool.txt" 2>&1 || true ;;
	esac
	echo "  $tool done"
done

for tool in ko_algtable ko_symmap ko_mmio; do
	[ -f "$(dirname "$0")/$tool.py" ] || continue
	"$PY" "$(dirname "$0")/$tool.py" "$KO" "$OUT/$tool.csv" > "$OUT/$tool.log" 2>&1 || true
	echo "  $tool done"
done

for tool in kv_analyze regdump_diff; do
	[ -f "$(dirname "$0")/$tool.py" ] || continue
	"$PY" "$(dirname "$0")/$tool.py" --help > "$OUT/$tool.help" 2>&1 || true
	echo "  $tool present"
done

ls -la "$OUT"
