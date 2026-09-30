#!/bin/bash
set -eu
H="${1:-http://192.168.10.1}"
: "${WEB_PASSWORD:?set WEB_PASSWORD to the web admin password}"
SALT=f2e816513d9cb58b008f4404af651581
DIR="$(cd "$(dirname "$0")" && pwd)/tmp"
mkdir -p "$DIR"
cd "$DIR"
rm -f wj.txt
curl -s -c wj.txt "$H/cgi-bin/luci/admin/wizard" -o wizard.html
CSRF=$(grep -o 'name="_csrf"[^>]*value="[^"]*"' wizard.html | sed 's/.*value="//;s/"//')
TOKEN=$(grep -o 'name="token"[^>]*value="[^"]*"' wizard.html | head -1 | sed 's/.*value="//;s/"//')
H2=$(node -e "const c=require('crypto');const s=x=>c.createHash('sha256').update(x).digest('hex');console.log(s(s(process.argv[1]+process.argv[2])+process.argv[3]))" "$WEB_PASSWORD" "$SALT" "$TOKEN")
curl -s -b wj.txt -c wj.txt -X POST "$H/cgi-bin/luci/admin/wizard" -D login_hdr.txt -o login_body.html \
	--data-urlencode "_csrf=$CSRF" --data-urlencode "token=$TOKEN" --data-urlencode "salt=$SALT" \
	--data-urlencode "zonename=UTC" --data-urlencode "timeclock=$(date +%s)" \
	--data-urlencode "luci_username=admin" --data-urlencode "luci_password=$H2"
grep -q "sysauth" login_hdr.txt && echo LOGIN-OK || echo LOGIN-FAIL
