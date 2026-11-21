# Vendor userspace reverse engineering — Cudy WR3000 v2.0 (luofu v2, firmware 2.4.15)

**Status: host-side static RE complete for the LuCI, cmagent/cmsd, bdinfo/hdb and upgrade
surfaces; kernel-side protocols and the EasyMesh/WLAN-driver ABI remain partially opaque.**
Date: 2026-11-21. Firmware: `2.4.15-20251030-114751` (R116), OpenWrt 22.03.6 base, kernel
5.10.201. Rootfs used: `/home/shibbityshwab/recovery-extract` (4027 files; byte-identical to the
notebook's local `rootfs-2.4.15` copy — verified with `diff -q` on the key binaries, e.g.
`usr/sbin/cmagent` sha256 `1acd50d5…` in both).

This report is host-side only: nothing here was executed on, read from, or written to a device.
No vendor binaries, extracted DTS or firmware blobs are committed; paths below refer to the local
extraction and the notebook's gitignored build area.

Related prior notebook work (not repeated here, cross-referenced):
[systime RCE writeup](../../systime-rce-writeup.md),
[MQTT/JWT command chain gist](../../../../gist-cudy-wr3000v2-mqtt-jwt.md),
[live MQTT command-chain report](../../../../dumps/router-20261108/mqtt-command-chain-report.md),
[vendor stack on mainline kernel](../../VENDOR-STACK-ON-MAINLINE.md).

---

## 0. How this was decompiled (reproduction)

The firmware ships **323 Lua 5.1 bytecode files** with a vendor-modified container. The header is
standard `\x1bLua\x51` with sizes `[endian=1, int=4, size_t=4, Instr=4, Number=8, trailer=4]`
— a 32-bit build whose 12th header byte is `0x04` instead of stock `0x00`, plus **one extra
constant type `9`** (4-byte signed integer constant). Stock Lua 5.1.5 rejects the file with
"bad header"/"bad constant". The recipe used here, which decompiles **all 323 files with zero
failures**:

1. Cross-build 32-bit Lua 5.1.5 (`make generic CC='gcc -m32'`) and patch `lundump.c`:
   - `luaU_header`: write `4` instead of the `is lua_Number integral` byte;
   - `LoadConstants`: add `case 9: LoadVar(int); setnvalue()`.
   This yields a host Lua that can `loadfile()` every vendor module and `luac -l` listings.
2. Transcode each vendor chunk to a standard 32-bit chunk (header byte 0, constant type 9 →
   type 3 double) with a ~120-line Python parser, then decompile with **unluac**
   (`java -jar unluac.jar`). Variable names are synthetic (`L2_1`) but control flow,
   constants, call targets and string data are exact.
3. For the stripped ARM binaries, use the notebook's
   `build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump` and **Ghidra 12.2
   headless** (`analyzeHeadless … -postScript DumpDecomp.java`). Ghidra output for
   `cmagent`, `cmsd`, `libbdinfo.so`, `hyctl` was the primary evidence for those binaries.

The existing notebook disassembler [tools/luadump.py](../../../../tools/luadump.py) works on the
same files but without the type-9 patch it silently mis-decodes a number of functions (e.g. the
`string.format` call sites in `apprpc/system.lua`); prefer the 32-bit/luac route when exactness
matters.

---

## 1. Platform and image facts relevant to the userspace

| Fact | Value / evidence |
|---|---|
| Firmware version | `/etc/rom_version` = `2.4.15-20251030-114751`; `/etc/rom_release` present, empty (release-image marker) |
| Board identity | `/proc/device-tree/model` second field → `R116` (scripts), `hsan_luofu_router_ax3000` vendor code strings |
| System type flag | `system.board.type` ∈ router / ap / re / client; `system.board.rom` = release type string |
| U-Boot env | `/etc/fw_env.config` → `/dev/mtd3` and `/dev/mtd4`, 0x40000 each (dual-slot A/B bootloader env) |
| MTD names used by scripts | `bdinfo`, `uboota`/`ubootb`, `fac`, `cfga`/`cfgb`, `log`, `upgrade`, rootfs/overlay (from `/proc/mtd` consumers: `/lib/hsan.sh`, `/etc/init.d/hsan_prepare`, `/lib/upgrade/platform.sh`) |
| Data partitions mounted at boot | `fac` → `/usr/local/factory`, `cfga` → `/config/work`, `cfgb` → `/config/workb`, `log` → `/log`, `upgrade` → `/upgrade` |
| Base packages | stock OpenWrt 22.03.6 + `dnsmasq-full`, `firewall4`, `wpad`, `mosquitto`, `openvpn`, `strongswan`, `chilli`, `cwmp`; vendor packages listed in §2 |
| Vendor encryption helper | `/usr/bin/crypt` (AES-128/192/256 CBC wrapper over OpenSSL; `-a` standard, `-g` "global" key derived from bdinfo, `-n` explicit hex key; `-e/-d`, `-i/-o`, `-eo`/`-gd` shorthands used throughout the vendor scripts) |
| Cryptographic literals | `libbdinfo.so` DES key literal `88T3j05dtFu8=`; mosquitto JWT plugin HS256 with bdinfo `secret`; cmagent AES key (see §4.4); cloud request-sign salt (see §5.1). All are firmware-wide constants shipped in every unit, not per-device credentials — reproduced here because they are required to understand/reproduce the protocols; per-unit values (`hmac`, `fuuid`, MACs) are **not** included. (Reconciled 2026-11-22: `secret` is a fleet-wide shared value, not per-unit - `crypt -gd` decrypts firmware-wide mosquitto certs at every boot with no per-unit re-encryption, and the mesh JWT model requires all peers to share the signing key; see docs/incoming/opaque-binaries/REPORT.md section 3.) |

Key artifact hashes (this image):

```
1acd50d5…  usr/sbin/cmagent          c779e2e5…  usr/sbin/cmsd
0ce7aff5…  usr/bin/bdinfo            8ba55259…  usr/lib/libbdinfo.so
4b5018a5…  usr/lib/auth_plugin_jwt.so (same as prior live unit)
f7831bca…  etc/mosquitto/mosquitto.conf   7a2626c4…  etc/mosquitto/aclfile.conf
```

---

## 2. Component map (vendor-specific userspace)

Full package list is in `/usr/lib/opkg/info/*.control`. Vendor components:

| Area | Files / packages | Role |
|---|---|---|
| Board data | `bdinfo-strict` (`/usr/bin/bdinfo`), `libbdinfo` (`/usr/lib/libbdinfo.so`), `/lib/functions/bdinfo.sh` | Encrypted board-info partition reader; identity/MAC/UUID/JWT secret source for everything else |
| HiSilicon appm stack | `hi_app` (script), `hi_appm` (hosted app "sysdev"), `hi_ipc`, `hi_cfm`, `hi_appenv`/getenv/setenv, `libhi_board/sysinfo/sysenv/basic/cfm/encrypt/upgrade`, `/home/cmd/**` | IPC command bus + board/sysinfo configuration + U-Boot sysenv variables + A/B firmware upgrade engine |
| Mesh / MQTT broker client | `cmagent` (+ `/usr/lib/lua/cmagent/**`), `mosquitto` + `auth_plugin_jwt.so` + ACL, `sync_command`, `sync_config`, `automesh`, `acmanager.lua` | Controller/satellite mesh control plane over MQTT; role = admin (`-a`) or satellite; AC/AP Wi-Fi configuration sync |
| Cloud daemon | `cmsd` (+ `cmsd-control`, `/usr/lib/lua/cmsd/{apprpc,script}.lua`), `/etc/token`, `/etc/cmsd/*` | MQTT-over-TLS client to `*.cudycloud.com`; JSON-RPC proxy into `luci.app` |
| Wi-Fi / HyFi | `softapd`, `hyfibrd`, `hyctl`, `hcshd`, `libhyfi-bridge`, SMT ubus methods, `wifi_init.sh`, `wifi_inner_init.sh`, `hi5622v100_cal_*`, `/etc/hi_wifi/*`, `/etc/wireless/{wsc,smt,wds,iface}` | WLAN control plane (SMT mesh, WSC/WPS, WDS, roaming, scan/acl), HyFi bridge glue into `hi_swa`/`hi_khslink`, RF calibration |
| EasyMesh | `easymesh`/`easymesh_cssd`/`easymesh_restore.sh`, `em_cli`, `libhi_emcore/embase/em_apis/emcss/em_openwrt_adpt`, `/etc/init.d/hsan_easymesh` | Wi-Fi Alliance EasyMesh controller/agent (ubus `em.*` objects, WPS onboarding, backhaul config, AP steering) |
| LuCI vendor layer | `/usr/lib/lua/luci/apprpc/*`, `controller/{autoupgrade,cwmp,mesh,network,servicectl,system,timewol,...}`, `model/cbi/{mesh,wan,lan,autoupgrade,system,...}`, `view/autoupgrade/*`, `luci-theme-cudy` | Web UI + JSON-RPC app API (mobile app/cloud) + vendor pages |
| Firmware update | `/sbin/autoupgrade`, `/usr/sbin/firmware.sh`, `/lib/upgrade/platform.sh`, `/sbin/oem-check`, `/usr/bin/fdt_dumpimage`, `/usr/bin/dumpimage`, `/usr/bin/fwtool`, `libhi_upgrade.so`, `/home/cmd/upgrade/*` | Cloud/local sysupgrade, image verification, A/B write, boot-slot switch |
| TR-069 | `cwmp`, `cwmp_cli`, `libcwmp.so`, `/usr/bin/tr069/*`, `/etc/config/cwmp` | CWMP/TR-098 client (disabled by default: `enable off`; conn-request 7547 `/tr069`) |
| Diagnostics | `hdb` + `/usr/bin/hdb_cmd/**`, `future`, `axelex`, `app_acs`, `app_oam`, `app_nlc`, `hi_md`/`hi_mw`, `/proc/hsan/**` | Debug counters, flow tracing, delayed commands, vendor ACS/OAM/netlink apps |
| Wi-Fi schedule | `/etc/config/wifi_schedule`, `luci.controller.wifischedule` | Scheduled Wi-Fi on/off, synced over MQTT |
| Ops helpers | `pingcheck`, `autoreboot`, `timewol`, `arpbind`, `port_trigger_d`, `smp.sh`, `memopt` | Vendor-added OpenWrt-ish helpers wired into `/etc/init.d` |

---

## 3. LuCI web UI

### 3.1 Request path, sessions, login

uhttpd serves `/cgi-bin/luci/*` through the Lua handler `/usr/lib/lua/luci/sgi/uhttpd.lua`
(`/etc/config/uhttpd` `lua_prefix`); `/www/cgi-bin/luci` is a 3-line shim that loads
`luci.sgi.cgi`. The dispatcher builds the vendor route tree from `luci.controller.*` and enforces
`sysauth` from `luci.main.sysauth` (default `admin`).

Login is a **client-side SHA-256 challenge**, not plaintext:

- `/www/luci-static/bootstrap/js/sysauth.js` computes
  `luci_password = sha256(sha256(password + salt) + token)` where `salt` is
  `luci.sauth.salt` (random 32-hex, created once by `/etc/uci-defaults/10_user`) and `token` is
  the per-session CSRF token from the ubus session.
- Server side (decompiled `luci/dispatcher.lua`, `htmlauth` authenticator): the submitted value
  must equal `sha256(crypt_decode(luci.sauth[user]) .. token)`; the per-user hash is stored
  `crypt -ea`-encrypted in `luci.sauth.<user>`. First login (`luci.sauth.defpasswd=1`, the
  factory state) stores `crypt(-ea)(submitted)` and flips `defpasswd=0`; this is the
  "create an administrator password" modal. Passwords must be 8..64 chars (64 hex after hashing);
  failure counters use `/var/lock` files with `flock`/`fuser` template variables produced by the
  dispatcher, giving the "exceeded N attempts" lockout on the login form.
- Session cookie: `sysauth=<ubus session id>; path=…; HttpOnly`; session timeout
  `luci.sauth.sessiontime=3600`. CSRF tokens are minted from `luci.sys.uniqueid(16)` and stored
  in the ubus session (`values.token`, 600 s validity).
- Factory/claimed states: `luci.main.wizard`/`factory` and `luci.sauth.defpasswd` select between
  wizard, login and the "managed by controller" screen (template
  `luci/view/themes/bootstrap/sysauth.htm` reads `cmagent.mqtt.broker` and disables login when the
  AP is claimed).
- **JSON-RPC** (used by the Cudy mobile app): `luci.controller.rpc` mounts
  - `POST /cgi-bin/luci/rpc/auth` (no sysauth) → `challenge` (login) / `logout`;
  - `/cgi-bin/luci/rpc/app` (sysauth via `authenticator` reading cookie or `auth` formvalue) →
    `luci.jsonrpc.handle(luci.app)`, i.e. every module in `luci/apprpc/`;
  - `/cgi-bin/luci/rpc/sysupgrade` (sysauth) → upload + `sysupgrade -T` + flash.

**Full app RPC surface** (decompiled `luci/app.lua` + `apprpc/*`): `feature(supported)`,
`conf(get_all,get_workmodes,get_system,get_ipv6,get_defaults,get_rate_limit,get_ddns,get_pingcheck,get_autoreboot)`,
`net(online_check,get_inetip,online_interfaces,iface_status,wifi_networks,set_wan,set_lan,set_dhcp,lan_wan_switch,set_ddns,set_pingcheck,set_usbtethering)`,
`eth(get_real_wan_port,switch_status,switch_status_2ps,switch_status_1ps,switch_phy_status,phy_status,getstatus)`,
`wifi(set_disabled,trigger_scan,get_aplist,try_join,forget,set_device,set_iface,set_mld,set_conf,set_guest,trigger_wps,get_wps_status,getiwinfo,get_freqlist,getstatus,wisp,join,get_wds_status,get_schedule,set_schedule)`,
`multi_ssid(*), mesh(set_disabled,reboot,reset,set_led,get_clients,get_devices,smt_init,smt_add,smt_devices,sync_config,upgrade_check)`,
`system(info,reboot,reset,leds,timezone,zonename,setclock,set_timeclock,setpasswd,autoupgrade,upgrade_check,upgrade_fwinfo,upgrade_download,upgrade_checkstatus,upgrade,bind_token,bind_success,auto_reboot)`,
`qsetup(set_workmode,set_timezone,set_zonename,set_autoupgrade,set_cellular,set_wan,wifi_set_iface,wifi_join,apply,cancel,set_conf)`,
`cellular(*), devices(*), iptv(*), ipv6(set_conf), vpn(*), parental_control(*)`.

### 3.2 Command sinks in the 2.4.15 LuCI tree (verified in bytecode)

| File (2.4.15) | Sink | Verdict |
|---|---|---|
| `luci/model/cbi/system/systime.lua` Flatpickr `current` write callback | `luci.sys.call(string.format("date -s '%s'", value))` with submitted `cbid.system.ntp.current` | **Injectible (root)**: single-quote breakout; this is the published RCE ([writeup](../../systime-rce-writeup.md)). Decompiled code shows the `timeclock` form value is read but unused in the write path; `on_commit` uses the numeric `os.date` path and is safe. |
| `luci/apprpc/system.lua` `setclock(v, force)` | `sys.call("date -s '%s'" % v)`; `force` bypasses the `/tmp/.timeclock` guard | **Second injectible sink**, reachable from any authenticated web session through `/rpc/app` and from the cloud via `cmsd` apprpc (§5) — not covered by the published 2.4.15 fix note. `set_timeclock` is the safe numeric sibling. |
| `luci/model/cbi/system/terminal.lua` | `luci.util.exec(formvalue("cbid.syslog.1.cmd"))` | Raw shell; route returns 403 as shipped (noted in prior writeup) |
| `luci/controller/system.lua` reboot/backup/restore actions | `fork_exec("sleep 1; /etc/init.d/uhttpd stop; reboot")`, `preset restore`, `sysupgrade -r /tmp/backup.tar.gz && preset defaults && reboot` | Fixed strings, safe |
| `luci/controller/rpc.lua`, `dispatcher.lua` | `echo -n '<value>' \| crypt -ea/-da`, `\| sha256sum` | Value constrained to 64 hex before use; safe by validation |
| `luci/controller/system.lua` sysupgrade action (line ~821) | `sleep 1; /etc/init.d/uhttpd stop; /sbin/sysupgrade %q` | Quoted, fixed path |
| `luci/controller/rpc.lua` `rpc_sysupgrade` | `sysupgrade -T %q` then `fork_exec("sleep 1; /etc/init.d/uhttpd stop; /sbin/sysupgrade %s %q")` | Quoted; requires auth |
| `luci/controller/index.lua` `action_bandwidth` | `io.popen("luci-bwc -i %q")` | Quoted, safe |
| `luci/controller/network.lua` WAN detect | `wantype -i %q > /tmp/wantype` | Quoted, safe |
| `luci/model/cbi/backup.lua`, `luci/controller/diag.lua` | `crypt -d -i %q -o /tmp/backup.tar.gz`, `tar -tzf %q`, `tar -C /var/run -zcf - diag \| crypt -g -e` | Quoted/fixed, safe |
| `luci/cbi.lua` file upload handler | `rm -rf /etc/luci-uploads/%s/*`, `mkdir -p …; mv %q.temp …/%q` | CBI-controlled names, quoted |
| `luci/apprpc/{wifi,eth,net,...}.lua` | many `fork_exec` on `/sbin/wifi`, interface names, `/etc/init.d/* reload` | Values from UCI/interface names; broad surface but not a direct shell sink from HTTP input |

### 3.3 Wizard (setup) flow

`luci.controller.index` routes `/admin/wizard` → `action_wizard` (renders `view/wizard.htm`) and
`/admin/guide` → `action_guide` (JSON list of steps from `luci.gui.show_wizard`). The wizard page
loads CBI sections over XHR, remembers `step`, supports Skip (`skip=1`), and on finish writes
`luci.main.factory=0`/`wizard=0`; "Exit" reverts all UCI changes (`uci revert`). Backend for the
network/quick-setup steps is `luci.apprpc.qsetup` (`set_workmode`, `set_wan`, `set_timezone`,
`wifi_set_iface`, `wifi_join`, `set_conf`, `apply`/`cancel`): work modes `router`/`ap`/`re`/
`client` and WAN protocols `dhcp`/`static`/`pppoe` (plus WISP/STA join path) are written into
network UCI, then applied with `/sbin/wifi` + network restarts. The same module is exposed to the
mobile app and cloud JSON-RPC.

### 3.4 System upgrade flow

Two independent paths share the vendor image checks:

**Local/Web upload** (`luci/model/cbi/system/upgrade.lua`):
1. upload handler writes `/tmp/firmware.img` (or `/upgrade/firmware.img` when an MTD partition
   named `upgrade` exists — the vendor image has one), creating `/tmp/upgrade_check_done` markers;
2. `sysupgrade -T` + platform check: `/lib/upgrade/platform.sh` `platform_check_image` →
   `oem-check -b R116 -o 0x80000 -f <img>` (RSA/CRC/board check), then `image_demux` (strip
   512 KB bootloader + 256-byte signature, extract FIT UBI with `fdt_dumpimage -T flat_dt -p 0 -w
   1048576`), then `hi_check_image` → IPC `hi_upgrade_image_check_file`;
3. Proceed → `sysupgrade <img>` (`stage2` in RAM; `platform_do_upgrade`): optional dual-U-Boot
   update (reads `OEM_BOOT xxx` version from `/dev/mtdX` at 511 KiB, compares with
   `oem_boot_version`), then `hi_do_upgrade` chooses slot flag = `!kernel_flag` and calls
   `hi_ipc /home/cmd/upgrade/hi_upgrade_image_write_file -v flag N filename … len … crc 0`,
   polling `hi_upgrade_flag_get` for success.

**Online/cloud update**: `/sbin/autoupgrade check|download|upgrade|report|auto` against
`https://[i18n|cn]-api[-test].cudycloud.com/device/v1/{auth,checkupdate}` and
`https://[event-]…cudycloud.com/device/v1/event`: POST JSON with MAC, `dtime`, `sign`
(md5 of fuuid+mac+hmac+dtime+hardcoded salt), `firmwarevr`, `region`, headers `fuuid`, `vr`
(from `/etc/rom_version`), `devtype` (uci `system.board.rom`), `lan`, `token`; reply saved to
**`/etc/fwinfo.json`** with `data.firmware = {vr,level,force,url,md5,msg}`; download honours
`level==2 && force==1` or `auto_upgrade=1`/`force`; verifies md5 then `sysupgrade -T`; upgrade
runs `sysupgrade /tmp/firmware.bin`. The same flow is exposed over the web UI
(`controller/autoupgrade.lua` + `view/autoupgrade/*`, status file
`/var/run/autoupgrade-status` with values check/downloaddone/timeout/fail/upgrade) and over MQTT
via `cmagent` `router/upgrade` (§4) and `/usr/sbin/firmware.sh` (which also reports progress
back to the controller via ubus `cmagent.router send`).

### 3.5 Network configuration pages

`controller/network.lua` maps status/config pages for WAN/LAN, switch, MAC clone, MWAN/wan2,
devices, internet restriction, VPN, DNS filter and a WAN-detect action
(`/sbin/wandetect all` + `wantype`). Page backends are `apprpc/net.lua` (`set_wan`/`set_lan`/
`set_dhcp`/`lan_wan_switch`), `apprpc/eth.lua` (`switch_status*`/`phy_status`, real-WAN-port
mapping via `/usr/bin/phyconfig` or `/bin/ethswctl`), `apprpc/wifi.lua` (radio/iface/guest/
multi-SSID/mesh sections; scan/join/WPS over `iwpriv`/softapd), `apprpc/ipv6.lua`,
`apprpc/iptv.lua`, `apprpc/vpn.lua`. Views under `view/wan/config.htm`,
`view/wireless/*.htm` call these through `cbi_xhr_load`; the Cudy mobile app calls the same
JSON-RPC methods.

---

## 4. cmagent — the MQTT mesh/control protocol

`/usr/sbin/cmagent` is a C daemon (stripped, ~75 KB) embedding a Lua 5.1 state for
`/usr/lib/lua/cmagent/<app>/<event>.lua`, plus ubus methods `cmagent.<m> {send,call,status,trigger}`.
Instance: `cmagent -m <prefix> -b <broker> -p <port> [-a] [-j] [-t timeout] [-l level] [-s]`
(`/etc/init.d/cmagent` starts it with UCI `cmagent.mqtt.*`; shipped config: broker
`127.0.0.1`, port `8883`, prefix `router`, timeout 30, admin 1).

### 4.1 Broker and authorisation

`/etc/mosquitto/mosquitto.conf` (2.4.15):

```
allow_anonymous false
auth_plugin /usr/lib/auth_plugin_jwt.so
acl_file /etc/mosquitto/aclfile.conf
listener 1883 0.0.0.0
listener 8883 0.0.0.0
cafile /var/etc/mosquitto/ca.crt
certfile /var/etc/mosquitto/server.crt
keyfile /var/etc/mosquitto/server.key
require_certificate true
```

The 8883 listener is mutual-TLS; its certificate material is shipped AES-encrypted in
`/etc/mosquitto/*.pem` and decrypted by `/etc/init.d/mosquitto` with
`crypt -gd … -o /var/etc/mosquitto/…`. The same dir holds the client cert/key cmagent uses for
8883. 1883 is plaintext. Both listeners use the same JWT plugin; plugin checks (prior work, gist
above): username contains bdinfo `deviceid`; password is HS256/384/512 JWT verified with bdinfo
`secret`; `exp` only has a floor (2019-12-31), never compared to clock; token `username` claim
contains `deviceid`. ACL (exact `user` match, so usernames are literals `cudydevice` /
`admincudydevice`):

```
user cudydevice        read  router/+/000000000000/+/+/+
user cudydevice        write router/000000000000/+/+/+/+
user admincudydevice   read  router/000000000000/+/+/+/+   ; $SYS/#
user admincudydevice   write router/+/000000000000/+/+/+
pattern write $SYS/broker/connection/%c/state
```

### 4.2 Identity, JWT minting (decompiled from the binary)

At startup cmagent reads bdinfo (via `libbdinfo`; `bdinfo_get_value`): `deviceid`, `secret`,
`fuuid` (+ two auxiliary fields). The advertised MQTT client id is
`<10 random printable>|<deviceid>` (random bytes from `/proc/sys/dev/flash_uuid` when its length
≥ 0x15, else `/dev/urandom`). MQTT username: plain `deviceid` for a satellite; with `-a`,
`admin<deviceid>` (`admincudydevice` on the shipped bdinfo) and the **topic identity** is forced to
`000000000000`. The MQTT password is a self-minted JWT: claims `{"username": <role username>,
"exp": now+300}`, signed HMAC using the bdinfo `secret` (`EVP_DigestSign` HS256; `-j` enables this
JWT auth path). No per-device credential escapes bdinfo.

### 4.3 Topic contract and subscriptions

Wire topic (publish format decoded from the binary, `%s/%s/%s/%u/%s/%s`):

```
<prefix>/<dst_did>/<src_did>/<seq>/<app>/<event>
```

`prefix` = UCI `cmagent.mqtt.prefix` = `router`; controller identity is the literal
`000000000000`; `seq` is a sequence/timestamp number (reply uses it as request id). Two
subscriptions are created at connect:

- admin: `<prefix>/<000000000000>/+/+/+/+` plus `$SYS/broker/+/clients/+/+`;
- satellite: `<prefix>/<own_did>/<controller_did>/+/+/+/+`.

The `$SYS` subscription feeds `/var/run/cmagent/<client_did>` state files and the `clients`
event (JSON `{"state":"connected|disconnected"}`). Message payload handling (decompiled):
if the port is not 1883 and the payload starts with `{` and ends with `}` two bytes before the
end (the *trailing NUL byte counts in payloadlen*; a bare JSON publish without NUL fails the sniff
in the shipped build), it is dispatched as JSON. The "crypt list" is a per-peer state machine:
a source id is added with count 0 on first traffic; a non-JSON payload from a listed peer
increments the count and is AES-decrypted; a JSON payload from a listed peer with count>0 is
*decrypted* (so the list means "this peer is currently speaking the encrypted channel"), and a
JSON payload from a listed peer at count 0 removes the entry and is processed as plaintext.
Sources that are not listed (including the controller `000000000000`) are processed as plaintext
JSON. Dispatch: `event == "reply"` → reply handler keyed by `atoi(app)`; else load/run the Lua
handler `/usr/lib/lua/cmagent/<app>/<event>.lua` with `(src_did, payload)`. Nothing in this
path authenticates the payload beyond the topic fields.

### 4.4 The "crypt list" AES channel

The binary contains a 16-byte AES-128 key literal `CLOUDmqttabc&123` (used only as a
protocol constant; it is not a per-unit secret). AES-CBC with an all-zero IV; payload is padded to
16 bytes. Decompiled behaviour:

- `publish`: if the broker port is 1883 (plaintext/dev), **all** payloads are AES-encrypted;
  otherwise plaintext unless the destination id is in the crypt list.
- `receive`: port 1883 → always AES-decrypt; else JSON sniff first, and if a sender is in the crypt
  list the non-JSON payload is AES-decrypted (counters in the list implement add/confirm/delete;
  ids are added on first non-JSON traffic from that peer, except `000000000000`).

This is an obfuscation layer, not authentication: the key is in every firmware image.

### 4.5 Apps/events handled by the Lua side (2.4.15)

Handlers exist under `/usr/lib/lua/cmagent/router/`; the contract as decompiled:

| app/event | Direction | Payload / behaviour |
|---|---|---|
| `router/command` | controller → device | `{"cmd":"<shell>","reply":0\|1}`; only if `src_did == "000000000000"`; runs `io.popen(cmd,"r")`, replies `{"code":0,"message":"success","data":"<output>"}` when `reply` set. **Root shell by design.** |
| `router/upgrade` | controller → device | `{"autoupgrade":"check\|download\|upgrade","lang":…,"url":…,"md5":…,"tries":…}`; admin branch runs `/usr/sbin/firmware.sh autoupgrade … <lang> &` or `/usr/sbin/firmware.sh <url> <md5> <tries> &` via `os.execute`; satellite branch stores state and publishes status. |
| `router/config` | controller → device | full UCI tree JSON: `{package: {section: {option: value\|[values], ".type": type}}}`; only applied when `src_did == "000000000000"`; walks the tree with uci set/add/set_list/delete, then restarts Wi-Fi/network/leds as needed. This is the AC→AP configuration channel. |
| `router/reply` | any | request/reply correlation with `seq` |
| `router/connection` | local event | `{"state":"connected"}`; on connect publishes `sysreport` and (satellite) `sysstatinfo`, clears `/var/run/cmagent/<src>`, sets l2portal server-ip. |
| `router/sysreport` | both | system report (CPU/mem load, radios/frequencies, wireless config incl. SSID/key/disabled for wlan03 and wlan01/wlan11/radio2, l2portal, NTP/auto-upgrade config, timezone…). The report contains Wi-Fi PSKs by design (sent to the controller). |
| `router/sysstatinfo`, `timer`, `upgrade_timer`, `clients`, `devlist`, `neighbor`, `roaming`, `ledctl`, `sta`, `wifischedule`, `rpc_smt` | telemetry/control | periodic discovery of wired/wireless clients (/proc/net, ifconfig counters), LED control, roaming/steering, interface list, mesh-SMT RPC, Wi-Fi schedule; published as JSON events under the same topic tree (decompiled `router/timer.lua`, `router/sysreport.lua`). |
| `router/rpc_smt` | controller → device | Simple-Mesh-Technology RPC (join/leave/steering) refilled from `/var/run/smt` by `acmanager`/`softapd`; used by the AC to drive satellites. |

`/usr/sbin/sync_command` (admin only) loops over `/var/run/cmagent/*` except `000000000000` and
sends `{"cmd": argv[1]}` to every satellite: the controller's "run this on all APs" primitive.
`/usr/sbin/sync_config` builds the UCI diff for a satellite (`cmagent.sync_config()`) or for an AC
via `acmanager.sync_config_by_clientId()`, then sends `router/config`.

### 4.6 Mesh discovery / adoption (automesh + acmanager)

- `/etc/init.d/cmagent` publishes mDNS service records via umdns when the device is a controller:
  `_cmagent._tcp.local` with `rpBA=<mac>;version=<rom>` (TXT) and, when AC mode is on,
  `_acmanager_ac._tcp.local` with `filter=<mac>` and `allow=<mac>` entries.
- `/usr/sbin/automesh` (admin only unless `mngt_proto=ac`) browses umdns
  (`ubus call umdns browse`, then queries `_cmagent._tcp.local`/`_acmanager_ac._tcp.local`),
  filters candidates by block/allow lists (TXT `filter=`/`allow=`), chooses a broker by:
  1. gateway match (candidate IP == WAN gateway), else 2. neighbour match (candidate rpBA ==
  `/proc/net/neighbor` peer), else 3. any; then rewrites UCI to switch the device into
  repeater/mesh-client mode (`system.board.type=re`, `cmagent.mqtt.broker=<ip>`,
  `cmagent.mqtt.mngt_proto`, `network.wan.mode=lan`, disables local mosquitto/dnsmasq, sets
  hidden backhaul STA vaps `wlan01/wlan11` `addr4=1`) and restarts the stack. Without Ethernet it
  uses the vendor SMT path: `ubus call wireless smt '{"iface":"wlan03","cmd":"automesh"|...}'`
  plus scans, and finally `broker=mesh.default:1883`.
- `/usr/lib/lua/acmanager.lua` is the controller-side AP manager (UCI package
  `apcontroller`): enumerates managed APs (`apcontroller.apclient`), per-client config
  (`gen_*_config`: backhaul SSID, AP SSIDs, L2 portal, network, IGMP, radio, system, Wi-Fi
  schedule), `sync_config_by_clientId`, group/SSID sync (`sync_ssid_config_by_group`), adoption
  (`_wired_adopt`/`_wireless_adopt`, `manageable_clients`, `mdns_allow_adopt`), offline tracking
  (`ap_offline_report/remove`, `/var/log/ap_offline`), and SMT RPC plumbing. Default UCI has no
  `apcontroller` package: this is a dormant AC feature on a standalone router.

### 4.7 Security summary for cmagent

1. `router/command` = unauthenticated **root command execution** for anyone who can publish a
   controller-addressed message; the only gate is the third topic field. Prior notebook work shows a
   forged satellite JWT reaches it on the tested unit (CVE-2026-71961 chain). The ACL compose
   makes this reachable with the satellite role that any holder of the fleet bdinfo `secret` can
   mint.
2. `router/upgrade` is a **second root command injection**: `url`, `md5` and `lang` fields are
   interpolated unquoted into `os.execute("/usr/sbin/firmware.sh …")`; `src_did`-gated only, same
   reachability as 1.
3. `router/config` lets a controller rewrite arbitrary UCI (including `luci.main.sysauth`,
   firewall, network) on a satellite; controller identity is a topic string, not a signature.
4. Non-TLS port 1883 exists by default and both listeners bind 0.0.0.0; the WAN firewall is the only
   fence. AES "crypt list" is obfuscation with a firmware-wide key.

---

## 5. cmsd — Cudy cloud client

`/usr/sbin/cmsd` (C, stripped, ~26 KB) is the second MQTT client, used for the vendor cloud
("Cloud Messaging Service Deamon"). It is disabled by default (`/etc/config/cmsd`:
`enabled 0`). `/usr/sbin/cmsd-control` is the shell side that acquires cloud credentials and
pushes the broker session into the daemon via ubus.

### 5.1 Bootstrapping (cmsd-control, decompiled script)

Endpoints (region/rdtest selected exactly like autoupgrade):

| Purpose | URL |
|---|---|
| auth | `https://{cn-api,i18n}[-test].cudycloud.com/device/v1/auth` |
| bind token | `…/device/v1/bindtoken` |
| IoT config | `…/device/v1/iot/getall` |
| client cert | `…/device/v1/createcert` |

Requests are JSON with `mac`, `dtime`, `sign = md5(fuuid+mac+hmac+dtime+"<hardcoded vendor salt>")`
and headers `fuuid`, `vr` (`/etc/rom_version`), `devtype` (uci `system.board.rom`), optional
`token: <jwt from /etc/token>`. A token is cached in `/etc/token`. `iot/getall` returns
`data.{certvr,addr,port,cid,topic.subscribe[],topic.publish[]}`; `createcert` returns
`rootcert`, `pubicpem`, `privatepem` which are stored **AES-encrypted** under `/etc/cmsd`
(`crypt -eo`) and decrypted at start to `/tmp/etc/cmsd/{ca,client,private}.pem`. The daemon is
then started with `ubus call cmsd init '{"addr","port","cid","topic","publish"}'`; an
`enabled==0` device sends `factoryreset:"enable"` to the bind endpoint on first bind, and
`cmsd-control start` only runs when `cmsd.cloud.enabled=1`.

### 5.2 Wire protocol (decompiled from cmsd + Lua handlers)

Messages are JSON published to the topics supplied by the server. Publish topic is
`chat/<cloud_cid>/<dest>` (dest from `send`), except when dest is `system`: then the fixed
publish topic from the server subscription list element `chat/sys/from` is used. Envelope:
`{"msgid":<monotonic random>, "msgtype":<int>, "dtime":<epoch_ms>, "data":<any JSON>,
"requestid":<id> (for 10001/10002)}`. Receive validation: `msgid` non-zero and not equal to the
last one; `dtime` present; `msgtype` present; optional `expire` (dtime+expire < now ⇒ drop);
`from` (destination id); `data`.

| msgtype | Direction | Handler |
|---|---|---|
| 0x2711 / 0x2712 (10001/10002) | request/response | logged (`recvmsg`); low-level keepalive/RPC |
| 0x4E21 / 0x4E22 (20001/20002) | cloud → device | **`/usr/lib/lua/cmsd/apprpc.lua`** — JSON-RPC 2.0 dispatcher over `luci.app` (i.e. the entire §3.1 apprpc surface: net/wifi/system/qsetup/devices/...). `combo.call` batches arbitrary methods. |
| 0x7531 (30001) | cloud → device (`from=="system"`) | schedules `script` handler with the **fixed** argument `cmsd-control update` (re-fetch config); rate-limited |
| unknown | cloud → device | replies 10002 with an error object |
| connect/disconnect/reconnect | internal | handler `script cmsd-control {start,update}` |

Lua handler loading: `/usr/lib/lua/cmsd/<handler>.lua`, function `service_call(from, data, …)`.
Only two handlers ship: `apprpc.lua` (JSON-RPC proxy) and `script.lua` (`os.execute(arg)`).
`script.lua` is only invoked by the daemon with fixed vendor strings in this build; the
cloud-controlled path is the **apprpc** one, which proxies into the same `luci.app` method set the
mobile app uses — including `system.setpasswd`, `system.setclock` (command-injection sink),
`system.upgrade`, `net.*`, `wifi.*`. Security consequence: a party that can speak the cloud
protocol to a bound device (possession of the per-device client certificate issued to that device,
or compromise of the vendor cloud) gets the full authenticated RPC surface without a web password.
The cloud channel is TLS with a per-device client cert; there is no additional payload signature.

---

## 6. Board / interface bring-up

### 6.1 Boot script chain

`/etc/rc.d/luofu` (invoked from `hsan_prepare` for the `luofu` compatible) loads the vendor
kernel modules in dependency order (kpinctrl, rng, cpufreq, acp, pie, lsw, mdio/phy/mac, kteam,
woc, cfe+kara+**khslink**, sensors, optional DFT), tunes `/proc/hsan/net/woc/skb_free` and
`/proc/hsan/cfe/l4/lrn_pkt_num` under memory pressure, and enables zram swap on 128 MB boards
(compare [vendor stack doc](../../VENDOR-STACK-ON-MAINLINE.md)).

`/etc/init.d/hsan_prepare` (S09) mounts `fac`/`cfga`/`cfgb`/`log`/`upgrade` MTD partitions,
creates device nodes/permissions, and `dev_init` loads USB PHY/HCD modules according to
`hi_cfm get board.usb*_num`. `/etc/init.d/hsan_start` (S15) clears `/upgrade` and runs
`/usr/bin/wifi_init.sh`. `/etc/init.d/hsan_appm` (S16) tunes sysctls, copies
`/etc/board-db/*` to `/config/conf/cfm/` if factory boardinfo is missing, seeds UCI
`system.board.ethernetPort*` from `hi_cfm`, inits the IPC modules (waiting for `/dev/hi_ipc`),
starts `hi_app` (the `hi_appm -n sysdev` app that owns `/home/cmd` handlers), and writes
`/sys/devices/platform/sysenv/boot_done`. `/etc/init.d/hsan_easymesh` starts
`easymesh_mgtd -d` and pushes EasyMesh config via `ubus call
em.{web_info,trigger_wps,ctrl_basic,steering_cfg,mandate_steering}`.

### 6.2 Network defaults

- `/etc/board.d/02_network`: R116 → WAN `eth3`, LAN `eth2 eth1 eth0`; LAN MAC from
  `bdinfo mac`, WAN MAC = LAN+1.
- `/etc/uci-defaults/98-board`: writes `system.board={name=R116, model=bdinfo model, ports=3,
  type=router}`.
- `/etc/uci-defaults/01_network`: LAN default `192.168.10.1`, adds `wisp` interface (proto dhcp,
  metric 80) to the WAN firewall zone, `network.wan.metric=60`.
- `/etc/uci-defaults/99_fixwan`: sets `network.wan.ori_macaddr` (LAN+1), per-port MAC devices,
  `wan2` dummy interface; for AP mode uses `mtd_get_mac_binary bdinfo 0xde00` (the binary
  factory MAC stored raw in the bdinfo partition — see §6.4).
- `/etc/uci-defaults/30_wlan` + `99_oem`: SSID `Cudy-<last 6 hex of MAC>`, WPA2/3 mixed, key from
  `bdinfo pin` (default `12345678`); radio MACs from bdinfo MAC/+2; hidden `wlan03`/`wlan13`
  backhaul vaps get **SSID = md5(bdinfo mac), key = md5(bdinfo hmac)** (`99_oem`), and EasyMesh
  backhaul/steering is configured by `hsan_easymesh`. Country, channel block lists and default
  channels are bdinfo-country driven. Those hidden vaps are effectively a mesh credential
  derivable from bdinfo fields, and their keys are part of the MQTT `sysreport` payload by design.
- `/etc/uci-defaults/{60_ipv6,69_ntp,81-wan-control,90_guest,99_firewall,98-cwmp,40_luci-*,...}`
  build IPv6/relay/464xlat, guest zone, WAN-control firewall include
  (`/usr/bin/wan_control.sh`), NTP defaults and CWMP/DIAG services.

### 6.3 Factory data and hi_cfm

`/usr/local/factory/{boardinfo,sysinfo}` (on the `fac` partition) are the board/sysinfo UCI
databases used by `hi_cfm`; the rootfs ships templates in `/etc/board-db/boardinfo_default` /
`sysinfo_default` and per-board examples in `/config/conf/cfm/{boardinfo_hsan_luofu_0,
sysinfo_hsan_luofu_0}` (version 2.0, ethnum/ge_num=4, wlan24/wlan58 flags, MACs, S/N, vendor id,
mode HGU, OMCI fields). `hi_cfm` CLI: `hi_cfm ?|check|config|show config|board [id]|
get/set board.<obj>|get/set sysinfo.<obj>|test restore|factory on/off/end`; it reads/writes the
factory UCI DB and `/config/conf/cfm`, selects a matching `boardinfo_<vendor>_<chip>_<board>`
template, and can `mtd erase rootfs_data` for factory reset. `hi_appenv`/`hi_getenv`/
`hi_printenv`/`hi_setenv` expose the kernel sysenv (/sys/devices/platform/sysenv) as
`key=value`; `hi_md`/`hi_mw` are devmem helpers.

### 6.4 bdinfo — the identity partition (complete format)

`/usr/lib/libbdinfo.so` (exports: `bdinfo_get_value`, `bdinfo_list_entry`, `bdinfo_check`,
`bdinfo_check_uuid`, `bdinfo_check_md5`, `bdinfo_check_dbg`, `bdinfo_version`,
`bdinfo_signature`, `dec_bdinfo`, `des_encrypt`, `mtd_fopen`, `parse_info_get[_n]`,
`parse_info_list_count`, `macstr_to_hex`, `b64_{en,de}code`, data `bdinfo_ori`, `bdinfo_dec`,
`RSA_PUB`, `RSA_SIGN_PUB`). Partition layout (verified by disassembly + Ghidra):

```
offset 0x0000 : uint32 BE  version                      (bdinfo ver: big-endian 4 bytes)
offset 0x0004 : DES-CBC ciphertext, 0xDD7C bytes       key = DES_string_to_key("88T3j05dtFu8="), IV=0
              :        (decrypts to 0xDD7C bytes of "key = value" lines, NUL after BDINFO_END)
offset 0xDD80 : RSA-1024 signature, 0x80 bytes          RSA_SIGN_PUB; payload = MD5(first 0xDD80 bytes)
offset 0xDE00 : 6-byte binary factory MAC (raw, unsigned region)
offset 0xDE06 : 0x10 bytes reserved/other
offset 0xDE16 : 0x80-byte second signature blob          written by "bdinfo sign <base64>"
total size   : 0xDE96 = 56982 bytes (mtd partition read whole)
```

- Every value read (`bdinfo_get_value`) first does a full read + MD5/RSA verification
  (`bdinfo_read`), so a corrupted/sig-broken partition makes **all** bdinfo queries fail (init
  scripts and `cmagent`/`cmsd` abort on `bdinfo checkuuid != OK`).
- Fields are parsed as `%s = %[^\n]`; documented consumers use: `mac`, `sn`, `deviceid`,
  `secret` (HMAC/JWT key), `fuuid`, `hmac`, `factory`, `board`, `model`, `country`, `pin`,
  `region`, `rdtest`, `dbg`, plus optional WireGuard/mesh keys
  (`private_key/public_key/endpoint_host/endpoint_port/addresses`; see `/lib/functions/bdinfo.sh`).
- `bdinfo check`: if `/etc/rom_release` exists, compare text `mac` (macstr_to_hex) with the raw
  binary MAC at 0xDE00; on mismatch log `mac addr error`, rewrite the raw area, write the whole
  0xDE96 buffer back to the partition, then run `checkuuid`. On release images the empty
  `/etc/rom_release` therefore enforces MAC/uuid consistency on every call.
- `bdinfo checkuuid`: compares the `fuuid` value with `/proc/sys/dev/flash_uuid` in three layouts
  (raw, hyphen-normalised, and a >68-byte long variant), returning 0 only on match. This is why
  the image refuses to run / cloud-register when fuuid is not bound to the SoC.
- `bdinfo check_md5`: read + `bdinfo_read` signature verify (skips if partition is all 0xFF).
- `bdinfo check_dbg`: /proc/sys/dev/flash_uuid must exist; requires bdinfo `factory`; fails if
  `/etc/rom_release` exists; passes if `/rom/etc/rom_develop` exists; otherwise requires
  `/etc/rom_dbg` to equal **hex(SHA256(flash_uuid + hmac + "@2025"))**. On this release image
  `bdinfo dbg` fails, which is why `/etc/uci-defaults/11_fix_passwd` derived the initial root
  password as `sha256(fuuid+hmac)` (see §10.6).
- `bdinfo sign <base64>`: requires checkuuid OK, base64-decodes exactly 0x80 bytes and writes them
  at 0xDE16, then writes the partition back (factory provisioning command; no private key in the
  library — the first signature is only verified with the embedded public key).
- CLI command set (decompiled): no args / unknown → usage; `bdinfo ver`, `bdinfo list`, `bdinfo
  md5`, `bdinfo dbg`, `bdinfo checkuuid`, `bdinfo check`, `bdinfo sign <b64>`, `bdinfo <key>`
  (prints `<key> = <value>`). `bdinfo list` prints `enc_fac_info:`/`count = N` followed by all
  entries (the only field-enumeration path).
- **Root password derivation (2.4.15)**: `/etc/uci-defaults/11_fix_passwd` sets the root password
  to `sha256(bdinfo fuuid + bdinfo hmac)` unless `bdinfo dbg == OK` or `system.ttylogin==1`.
  Both inputs are bdinfo fields; on units where those fields are shared/derivable this yields a
  predictable root password (§10.6).
- Recovery tooling in the notebook: [tools/decrypt_bdinfo.py](../../../../tools/decrypt_bdinfo.py),
  [tools/mk_fake_bdinfo.py](../../../../tools/mk_fake_bdinfo.py).

### 6.5 hdb / hi_* / misc helpers

- `/usr/bin/hdb` is a dispatcher: `hdb <group> <cmd> [args]` executes
  `/usr/bin/hdb_cmd/<group>/<cmd>`. Shipped: `cnt get|clr` (walks `/proc/hsan/net|cfe|lsw` counter
  files), `eth speed <dev> <speed> <duplex>` (ethtool), `flow cnt|learn|trace` (write options to
  `/proc/hsan/diagnose/flow/*`), `info dump [server-ip]` (collects system state to
  `/tmp/hdb_info_<chip>_<date>`, optional TFTP upload). `hdb` with a group alone prints each
  script's `-h`.
- `hi_ipc <descriptor> [key value ...]`: sends one command from `/home/cmd` to the `hi_appm`
  sysdev over `/dev/hi_ipc`. Descriptors are INI-like: `[settings] functionname=<C symbol>;
  attributenum=N; attributeflag=1` then `[parameter] attribute=alias<name> type<…> range<…>
  default<…>`. Shipped descriptor groups: `upgrade/` (`hi_upgrade_flag_get/set`,
  `hi_upgrade_image_write_file`, `hi_upgrade_image_check_file`, `hi_upgrade_cmd`,
  `hi_upgrade_dump`, `hi_img_size`, `dl_download_remote_file`, `dl_nc_upgrade_start`),
  `voice/` (adpt/slic/voip/test), `pon/` (GPON/EPON/OMCI operations, 50+ commands). This is the
  userspace↔firmware IPC ABI; handler implementations live in the stripped hi_appm ecosystem.
- `/usr/bin/future`: delayed-command scheduler (`future N cmd args…`; `future 0` cancels; jobs in
  `/tmp/future`, pid file, `system()`); used by autoupgrade/upgrade to schedule cleanup/report.
- `/bin/axelex` (upload-diagnostic service), `app_acs` (auto channel selection service talking to
  the WiFi driver), `app_oam` (OAM/debug-kit socket app), `app_nlc` (Hisilicon netlink client),
  `pingcheck` + `/etc/pingcheck/*.d` hooks for WAN online/offline events (vendor adds
  wisp/apmode/checkip hooks).

### 6.6 fwinfo

The image itself carries no separate `fwinfo` partition; the name appears in two roles:
(a) **`/etc/fwinfo.json`** — the cached online-update answer from
`device/v1/checkupdate` (`{"data":{"firmware":{"vr","level","force","url","md5","msg"},…}}`)
used by the update page, `/sbin/autoupgrade` and the MQTT upgrade handler; (b) the FIT image
metadata (OpenWrt `fwtool` / `/tmp/sysupgrade.meta` with `supported_devices`,
`compat_version`) consumed by `/lib/upgrade/fwtool.sh`. Image layout is documented in
`/lib/upgrade/platform.sh` itself: `[bootloader 512KB][sig 256B][pad to 2KB][FIT{kernel_fit +
UBI(image+rootfs)} + metadata]`, with `oem-check -b <board> -o 0x80000` verifying
name/header CRC/data CRC/RSA signature and `fdt_dumpimage -T flat_dt -p 0 -w 1048576` extracting
the UBI subimage.

---

## 7. Wi-Fi management and HSLINK / HyFi

### 7.1 WLAN control plane

- `/usr/sbin/softapd` (230 KB; ubus, netlink, json-c, UCI, OpenSSL) registers the vendor
  `wireless` ubus object with handlers `up/down/notify/smt/smton/smtoff/wps*/wds*/roaming/
  aclchk/debug/cleanup`; scan/WPS/ACL go through vendor driver ioctls (`iwpriv <if>
  ap_chan_scan`, `WLAN_GetAllStaStatus`, `nlwifi_set_app_ie`) — i.e. the Hi5622 `hsanwifi`
  driver ABI, not nl80211. `smt` is the vendor Simple-Mesh-Technology tunnel: IE payload with
  SSID/auth/PSK and a fixed default string `CUDYMeshSMT_1234`; `wlan01`/`wlan03` are the SMT
  STA/AP vaps. This is the wireless-side discovery used by `automesh` when no Ethernet backhaul
  exists.
- `/usr/bin/bandsteering` is a small `hi_ipc`-based steering daemon (band/BSS selection hints to
  the WiFi driver via the sysdev IPC, not through hostapd).
- `wifi_init.sh` selects the RF stack by `hi_cfm get board.wifi_vender`: `wuyi`
  (hi5620v100/sky2+oal/oam/plat/frw/sdt/hal/dmac/hmac/wal/alg modules + cal), `tiangong1/2`
  (wifi_plat/wifi_host + `hi_khslink` + `npu_init.sh` + wifi_cfg firmware), or `shuangta`
  (hi5622v100_plat/wifi) — matching the module tree in `/lib/hisilicon/ko`.
  `wifi_inner_init.sh` copies `/lib/firmware/hi_wifi/wifi_cfg*` into `/config/work/firmware`
  when the chip type requires it. Calibration helpers: `hi5620v100_cal_{get,init}.sh`,
  `hi5622v100_cal_{get,init}.sh`, `Hi5622V200_cal_{init,save}.sh` (parse/write
  `/usr/local/factory/*.cal`). Wi-Fi schedule config lives in `/etc/config/wifi_schedule` and is
  applied by `/etc/wireless/smt|wsc|wds` hotplug scripts + `luci.model.cbi.wifischedule`.
- `/etc/init.d/wpad`, `/usr/sbin/wpad` (hostapd/wpa_supplicant composed binary) are stockish but
  fed vendor driver config by softapd's `wsc`/`smt` handlers.

### 7.2 HyFi bridge (HSLINK userspace)

The QCA-derived "HyFi bridge" (`kmod-qca-hyfi-bridge`, `/usr/lib/libhyfi-bridge.so`) is the
bridge-forwarding shim between `hi_swa`/`hi_khslink` and the driver; the userspace pieces:

- `/usr/sbin/hyfibrd` — ubus object `hyfibr` (`init`, `setgrp`, `setbcast`, `ports`,
  `bstevt`, `debug`). `/etc/init.d/hyfibrd` calls `hyctl attach br-lan` (router/re) or
  `br-wan` (ap), then `ubus call hyfibr init '{"iface":…,"mode":"relovr"|"grprel",
  "fwmode":"APS"|"ISOLATE"|"SINGLE"|"MCAST"}'`; `bstevt` and `setgrp` drive broadcast/multicast
  handling. Strings document modes `SINGLE/MCAST/ISOLATE`, bridge attach/detach, HA/HD table
  flush, per-port type (`w2|w5|plc|eth|w6|mld`).
- `/usr/sbin/hyctl` — CLI over `libhyfi-bridge`: `show/attach/detach`, HD/HA/forwarding table
  add/get/delete, `setbrmode` (relovr/grprel), `setfwmode`, `setifgrp`, `setifbcast`,
  multicast snooping/ACL/aging/route controls, `settcpsp` — full command list recoverable with
  `hyctl` strings.
- `/usr/sbin/hcshd` (9 KB) — a tiny **network command receiver**: binds a UDP socket, reads a
  command, RSA-decrypts it with an embedded 1024-bit RSA public key (same key material as
  libbdinfo `RSA_SIGN_PUB`), logs and executes it with `system()/popen()`. It starts very late
  (S98) and no shipped script feeds it; treat it as a dormant root-command daemon and audit its
  listening port/firewall exposure before enabling anything on a network.
- Kernel side: `hi_khslink.ko` (+ `hi_swa`) implements the actual HSLINK packet path; see
  [vendor stack analysis](../../VENDOR-STACK-ON-MAINLINE.md) and the notebook's phase docs. The
  userspace↔kernel interfaces are via these ubus objects and the `/proc/hsan/` tree
  (lsw/cfe/net counters used by `hdb` and start scripts).

---

## 8. Vendor binaries — purpose and interface summary

| Binary | Type | Interface / format |
|---|---|---|
| `/usr/bin/bdinfo` + `/usr/lib/libbdinfo.so` | ELF / .so with dynsym | §6.4 (CLI `ver/list/md5/dbg/checkuuid/check/sign/<key>`; encrypted 0xDE96 partition) |
| `/usr/sbin/cmagent` | C, stripped, embeds Lua | §4 (`-m/-b/-p/-a/-j/-t/-l/-s/-v`, ubus `cmagent.*`, MQTT topics, AES channel) |
| `/usr/sbin/cmsd` | C, stripped, embeds Lua | §5 (`-a/-j/-l/-b/-p/-m/-s/-t`, ubus `cmsd.{init,update}`, cloud topics/envelope) |
| `/usr/sbin/cmsd-control` | shell | §5.1 (auth/bindtoken/iotgetall/createcert, cert storage, daemon start/update) |
| `/sbin/autoupgrade` | shell | §3.4 online update CLI (`check/download/upgrade/report/auto`) |
| `/usr/sbin/firmware.sh` | shell | cloud/MQTT upgrade bridge: `download_file <url> <md5> <tries>`, `upgrade_firmware`, `autoupgrade check\|download\|upgrade\|report`; reports via `ubus call cmagent.router send` |
| `/lib/upgrade/platform.sh` | shell | §3.4/§6.6 image demux, `oem-check`, `hi_upgrade_image_*` IPC, dual-U-Boot version update |
| `/sbin/oem-check` | ELF | `-b <board> -o <bootloader size> [-r][-t] -f <file>`; parses vendor firmware header + FIT/UBI, RSA verify (embedded key), CRCs; prints `firmware verify ok` |
| `/usr/bin/fdt_dumpimage` | ELF (U-Boot dumpimage derivative) | `-T type -l\|-p pos -w window -o out file`; only `flat_dt` list/extract; used to pull UBI from FIT |
| `/usr/bin/dumpimage` | ELF (U-Boot) | generic U-Boot image/FIT inspect/extract |
| `/usr/bin/fwtool` | ELF (OpenWrt) | `-q -s/-i/-T`, FIT metadata/signature extraction |
| `libhi_upgrade.so` | .so | A/B image engine: `hi_upgrade_{image_write_file,image_check_file,flag_get/set,cmd,sharding_write,pkg_*,block_*}`, `hi_upg_get_img_{version,size}`, onetrack parser (kernel/rootfs/java), erases/writes MTD shards, sets `/sys/devices/platform/sysenv/boot_reg`, `/tmp/upgradeSuccess`, reboots |
| `hi_appm`/`hi_app`/`hi_ipc`/`hi_cfm`/`hi_appenv`/`hi_md`/`hi_mw` | mix of ELF/shell | §6.3/§6.5 IPC bus and board/sysenv CLIs |
| `hyfibrd`/`hyctl`/`hcshd` | ELF | §7.2 HyFi bridge daemon/CLI and UDP RSA-signed command receiver |
| `softapd`/`bandsteering`/`app_acs`/`app_oam`/`app_nlc` | ELF | §7.1 WLAN control plane, ACS/OAM/netlink vendor driver companions |
| `easymesh`/`easymesh_cssd`/`em_cli` + `libhi_em*` | ELF | EasyMesh controller/agent/CSS daemon and CLI (`em_cli <object> <method> args`, ubus `em.*`); config pushed from `hsan_easymesh` |
| `cwmp`/`cwmp_cli` + `libcwmp.so` | ELF | TR-069/TR-098 CWMP client; config `/etc/config/cwmp` (disabled; conn request 7547 `/tr069`; ACS discovery via DHCP option/ACS URL, not in rootfs) |
| `crypt` | ELF | §1 AES file encrypt/decrypt (`-e/-d` × `-a/-g/-n`, `-l/-k/-v/-i/-o`, `-eo`/`-gd` shortcuts); `-g` derives a global key using bdinfo `board` |
| `future` | ELF | §6.5 delayed command scheduler |

---

## 9. Decoded protocols and formats (quick index)

1. **MQTT mesh control** — §4: topic `router/<dst>/<src>/<seq>/<app>/<event>`, roles admin
   (`000000000000`) / satellite; JSON with optional trailing NUL; AES-CBC fallback with firmware
   key; Lua handlers per app/event; ACL role matrix; JWT auth with bdinfo secret.
2. **Cloud MQTT (`chat/…`)** — §5: envelope `msgid/msgtype/dtime/data/requestid/from/expire`;
   msgtypes 10001/10002, 20001/20002 (JSON-RPC apprpc), 30001 (config update); topics from server.
3. **Cloud bootstrap HTTP** — §5.1: `/device/v1/{auth,bindtoken,iot/getall,createcert}`, JWT token
   in `/etc/token`, md5 sign with bdinfo fields, certs encrypted with `crypt`.
4. **bdinfo partition** — §6.4: 0xDE96 bytes, DES-CBC payload, RSA-1024 MD5 signature, raw MAC,
   second signature slot, `key = value` field list, CLI semantics.
5. **sysupgrade image** — §3.4/§6.6: 512 KB bootloader + 256 B signature + FIT(kernel+UBI) +
   metadata; `oem-check` verify; FIT subimage extraction; A/B slot flags via sysenv `boot_reg`.
6. **U-Boot environment** — dual `/dev/mtd3`/`/dev/mtd4`, 0x40000 each (`/etc/fw_env.config`).
7. **HyFi/HSLINK userspace ABI** — §7.2: ubus `hyfibr` API + `hyctl` command surface, kernel
   `hi_khslink`; SMT vendor tunnel IE via softapd.
8. **EasyMesh control** — §7.1: `em.*` ubus methods, WPS/backhaul/steering config from UCI
   `easymesh_default`, `easymesh_mgtd` process set.
9. **LuCI login/CSRF/cookie format** — §3.1: `sha256(sha256(pw+salt)+token)`,
   `sysauth=<sid>`, `luci.sauth` storage, `/rpc/auth` challenge.
10. **Vendor AES file format (`crypt`)** — §1/§8: header-free AES-CBC over file data, key modes
    `-a/-g/-n`; used for certs, backups, password storage (`crypt -ea`).
11. **fwinfo.json** — §6.6: `data.firmware.{vr,level,force,url,md5,msg}`.

---

## 10. Security-relevant findings (new/verified in 2.4.15)

Numbered for the ledger; all are firmware-level. Per-device secrets are not reproduced.

1. **LuCI systime command injection (verified).**
   `luci/model/cbi/system/systime.lua`, Flatpickr `current` write callback:
   `luci.sys.call(string.format("date -s '%s'", value))`; single-quote breakout ⇒ root RCE for any
   authenticated admin session. Same finding as the existing
   [writeup](../../systime-rce-writeup.md); decompilation here confirms the exact callback and
   that `on_commit` uses the safe numeric path.
2. **Second systime injection via the app RPC layer.**
   `luci/apprpc/system.lua#setclock(v, force)` formats `date -s '%s'` with the raw RPC argument;
   `force` bypasses the `/tmp/.timeclock` guard. Exposed at `/cgi-bin/luci/rpc/app` (authenticated)
   and — importantly — through the **cloud `cmsd` apprpc channel** (§5.2). Not covered by the
   published 2.4.15 fix note (which only covers the CBI page).
3. **MQTT `router/command` = root command execution** (prior CVE-2026-71961 chain). Confirmed
   from bytecode: `cmagent/router/command.lua` checks only `src_did == "000000000000"` then
   `io.popen(cmd)`. Reachable with a forged satellite JWT (fleet bdinfo secret) publishing a
   controller-addressed message; no second node required on the tested unit.
4. **MQTT `router/upgrade` = second root injection.** `cmagent/router/upgrade.lua` builds
   `/usr/sbin/firmware.sh autoupgrade check <lang> &` and
   `/usr/sbin/firmware.sh <url> <md5> <tries> &` with unquoted payload fields via `os.execute`
   (`src_did`-gated only). A controller-role publisher can inject shell commands without using
   `router/command` at all.
5. **Unauthenticated-by-design mesh config channel.** `router/config` applies controller-supplied
   UCI trees (including `luci.main.sysauth`, firewall, network, Wi-Fi keys) on a satellite; the
   MQTT topic's third field is the only identity. With the fleet secret and ACL role composition a
   satellite's config is controller-writable by anyone who can publish in the satellite role.
6. **Predictable factory root password.** `/etc/uci-defaults/11_fix_passwd` sets root's password to
   `sha256(bdinfo fuuid + bdinfo hmac)` on release images (where `bdinfo dbg` fails). Both values
   come from the bdinfo partition. On units sharing fuuid/hmac (or where hmac is derivable from
   fleet material), the root password is derivable from the same dump that yields the JWT secret —
   treat "bdinfo dump ⇒ root shell" as the real boundary.
7. **Hidden mesh AP credentials derivable from bdinfo.** `99_oem` sets hidden `wlan03`/`wlan13`
   SSID = md5(bdinfo mac), key = md5(bdinfo hmac); both are also included in `router/sysreport`
   payloads. Anyone with the MAC/hmac pair knows the hidden AP PSK (mesh onboarding surface).
8. **Cloud→device RPC without web credentials.** `cmsd` msgtypes 20001/20002 proxy JSON-RPC into
   `luci.app` (`apprpc`), including `system.setpasswd`, `system.setclock`, `system.upgrade`,
   network/Wi-Fi setters. The only gates are the per-device TLS client certificate (issued by the
   vendor cloud) and server-side auth; there is no per-request signing. A leaked or cloned
   `/etc/cmsd/private.pem` is a persistent remote-control credential.
9. **`hcshd`** executes RSA-decrypted commands from the network as root via `system()/popen()`
   (no shipped caller; audit/reachability before enabling; not exposed by any init script).
10. **Broker exposure/hardcoded crypto.** 1883 plaintext + 1883/8883 both `0.0.0.0`; JWT checks
    use substring matching and a fixed expiry floor (prior gist); AES "crypt list" key is a
    firmware literal; bdinfo DES key literal and RSA public keys are firmware-wide constants.
11. **Broad authenticated RPC surface.** `/cgi-bin/luci/rpc/app` exposes the whole `apprpc` set
    (password change, command-sink `setclock`, upgrade, Wi-Fi/network, devices) behind only a
    session cookie/token; CSRF for this endpoint relies on the ubus session token in the password
    challenge, not on the CBI CSRF token.

Mitigations already in the release image: WAN firewall input REJECT, no anonymous MQTT, 8883
requires client certs, `bdinfo` MD5/RSA verification gates all bdinfo-dependent daemons, and
`/etc/config/cmsd.enabled=0` by default. None of them changes findings 1–8 once an attacker has a
web session, a broker credential, or cloud-channel access.

---

## 11. What is still opaque, and what 100% coverage would need

| Area | Why still opaque here | What would close it |
|---|---|---|
| `hi_appm` handler implementations (voice/PON/upgrade internals) | Hosted in stripped `hi_appm` + `libhi_upgrade.so`; only descriptor schema + symbol names recovered | Ghidra decompile of `hi_appm`, `libhi_upgrade.so`, `libhi_*board/sysinfo/encrypt` with demo descriptor inputs; or device-side tracing of `/dev/hi_ipc` |
| WLAN driver ABI (vendor `hsanwifi`) | Vendor ioctls/netlink in `hi5622v100_wifi.ko` / `softapd`; userspace side only string/ubus-level | Kernel-side RE (tracked in `docs/phase*`); debug firmware; softapd↔driver tracing |
| HSLINK packet format | `hi_khslink.ko` only; userspace ubus API decoded but on-wire format not in rootfs | Kernel RE + live captures |
| EasyMesh protocol | `libhi_emcore.so` (768 KB) not decompiled; only ubus surface + config flow decoded | Ghidra on `libhi_emcore/embase` + `em_cli`, cross-referenced with WFA EasyMesh spec |
| Cudy cloud server behaviour | Only client side: endpoints, request fields, message envelope; server auth/response semantics inferred from handler code | A captured cloud session (own device, authorized) or vendor API docs |
| `crypt -a/-g` key derivation | Global key derivation reads bdinfo `board` + runtime material but the exact KDF was not fully reversed | Decompile `crypt` fully (9 KB) or instrument a running device; affects backup/cert confidentiality, not auth |
| U-Boot env content / A-B boot variables | U-Boot standard format; values are device state, not in rootfs | `fw_printenv` on a device (read-only) |
| bdinfo field generation (hmac, sn, pin, region) | Field semantics decoded; factory-time generators are not in the rootfs | Factory image / vendor source, or controlled bdinfo comparison |
| TR-069 (CWMP) | Disabled and only config-level decoded (`libcwmp.so` 256 KB) | Decompile `libcwmp.so` + capture an ACS session |
| Extracted DTS / board data | Not in rootfs by design | Not needed for userspace; see kernel docs |

Reaching "100%": (a) decompile the remaining `libhi_*`/`hi_appm`/`libhi_emcore`/`libcwmp` binaries
with Ghidra and annotate by caller graph; (b) dynamic confirmation on an authorized unit with
read-only captures (MQTT with a test broker, `/dev/hi_ipc` tracing, softapd↔driver tracing);
(c) server-side cloud captures for the cloud channel; (d) kernel module RE for HSLINK/WLAN. The
first three are host-capable and bounded; the last is the effort already tracked in the notebook's
kernel phases.

---

## 12. Evidence index / reproduction

- Rootfs: `/home/shibbityshwab/recovery-extract` (not committed). Local copy
  `rootfs-2.4.15/` is byte-identical for the key files verified by sha256.
- Lua decompile pipeline: 32-bit Lua 5.1.5 build + `lundump.c` patch (header byte 4, constant
  type 9) → Python transcoder → unluac. Scratch artifacts were under `/tmp` and are deliberately
  not committed.
- Cross-disassembly: `build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump`;
  Ghidra 12.2 headless `DumpDecomp.java` (scripts temporary).
- Existing notebook tooling used/cross-checked: [tools/luadump.py](../../../../tools/luadump.py),
  [tools/decrypt_bdinfo.py](../../../../tools/decrypt_bdinfo.py),
  [tools/mk_fake_bdinfo.py](../../../../tools/mk_fake_bdinfo.py),
  [tools/forge_jwt.py](../../../../tools/forge_jwt.py),
  [tools/mqtt_takeover.py](../../../../tools/mqtt_takeover.py).
- Prior writeups cross-checked: [systime RCE](../../systime-rce-writeup.md),
  [MQTT/JWT gist](../../../../gist-cudy-wr3000v2-mqtt-jwt.md),
  [live command-chain report](../../../../dumps/router-20261108/mqtt-command-chain-report.md),
  [vendor stack](../../VENDOR-STACK-ON-MAINLINE.md).
- All findings are static, against firmware 2.4.15. No device interaction was performed for this
  report; the bench remains read-only per [AGENTS.md](../../../../AGENTS.md) and
  [BENCH-FACTS.md](../../../../BENCH-FACTS.md).
