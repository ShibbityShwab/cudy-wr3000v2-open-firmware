# Vendor userspace RE — summary (Cudy WR3000 v2.0, firmware 2.4.15 R116)

Full report: [REPORT.md](REPORT.md). Host-side static RE only; no device interaction; no vendor
binaries or blobs committed. Rootfs: `/home/shibbityshwab/recovery-extract` (byte-identical to the
notebook's local `rootfs-2.4.15` copy for the key binaries).

## What was decoded (high confidence)

| Surface | Result |
|---|---|
| Lua userspace | **All 323 LuaQ bytecode files decompiled with zero failures.** Vendor container quirks found and solved: 12th header byte `0x04`, extra constant type 9 (4-byte int). Recipe: patched 32-bit Lua 5.1.5 + transcoder + unluac. |
| LuCI auth/session | Client-side SHA-256 challenge (`sha256(sha256(pw+salt)+token)`), `luci.sauth` crypt storage, ubus session cookie `sysauth`, CSRF token, lockout, and the JSON-RPC mount points `/rpc/auth`, `/rpc/app` (whole `luci.apprpc` surface, 100+ methods enumerated), `/rpc/sysupgrade`. |
| LuCI sinks / in 2.4.15 | Systime `date -s '%s'` RCE (verified), **second injection** in `apprpc/system.lua#setclock` (reachable from web RPC and cloud), terminal.lua raw exec, plus a table of all shell/fork_exec call sites marked safe/unsafe. |
| Web flows | Wizard/setup (routes, steps, skip/revert semantics), local upgrade (upload → `oem-check` → FIT demux → `hi_upgrade_image_*` IPC → A/B slot flip), online upgrade (`/sbin/autoupgrade` endpoints, request/sign, `fwinfo.json`, status machine), network pages (controller routes + apprpc net/eth/wifi/ipv6/iptv + views). |
| cmagent MQTT mesh | Exact topic `<prefix>/<dst>/<src>/<seq>/<app>/<event>`, admin vs satellite roles, ACL role matrix, JWT self-minting from bdinfo `secret` (exp+300 s), MQTT client-id format, JSON sniff (trailing NUL), **AES-128-CBC "crypt list" channel with firmware key**, full router app/event catalog (command/upgrade/config/reply/connection/sysreport/timer/clients/devlist/neighbor/roaming/ledctl/sta/wifischedule/rpc_smt), sync_command/sync_config, automesh mDNS adoption (`_cmagent._tcp`/`_acmanager_ac._tcp`, gateway/neighbour/any selection, RE-mode UCI rewrite), AC-side `acmanager.lua` config generators. |
| cmsd cloud | Four endpoints, token flow, md5-signed bootstrap, cert issuance + `crypt -eo` storage, cloud MQTT envelope (`msgid/msgtype/dtime/data/requestid/from/expire`), topic scheme `chat/<cid>/<dest>`, msgtype dispatch (10001/2, 20001/2 → JSON-RPC into `luci.app`, 30001 → fixed control update), Lua handler loading. |
| bdinfo / libbdinfo | **Complete partition format**: 0xDE96 bytes; BE version; DES-CBC payload (0xDD7C, key = `DES_string_to_key("88T3j05dtFu8=")`, zero IV); RSA-1024 signature at 0xDD80 over MD5(first 0xDD80); raw binary MAC at 0xDE00; second 0x80 signature slot at 0xDE16 (`bdinfo sign`); full CLI (`ver/list/md5/dbg/checkuuid/check/sign/<key>`) and semantics of check/checkuuid/check_dbg; field list and consumers. |
| Board bring-up | Init chain (`luofu` module order → `hsan_prepare/start/appm/easymesh`), MTD mounts, board.d R116 port map (WAN eth3, LAN eth2/1/0), UCI defaults (LAN 192.168.10.1, MAC scheme, WISP, guest, IPv6, NTP, WAN control), factory `hi_cfm` board/sysinfo DB, hi_* IPC bus and `/home/cmd` descriptor format, `hdb`/`future`/`crypt` helpers, Wi-Fi init variants and hidden mesh vaps. |
| Upgrade image / tools | Layout `[512 KB bootloader][256 B sig][FIT(kernel+UBI)+metadata]`; `oem-check -b R116 -o 0x80000` checks; `fdt_dumpimage -T flat_dt -p 0 -w 1048576` UBI extraction; dual U-Boot env on mtd3/mtd4; `libhi_upgrade` A/B engine symbols and boot_reg/boot-success protocol; `fw_env.config`. |
| Wi-Fi / HyFi / HSLINK userspace | softapd vendor `wireless` ubus API (SMT/WSC/WDS/roaming/ACL, vendor iwpriv ABI), wifi_init variants and cal scripts, EasyMesh `em.*` ubus surface, HyFi bridge `hyfibr` ubus API + full `hyctl` command surface, `hcshd` RSA-signed UDP command receiver. |

## Security-relevant findings (see REPORT §10 for evidence)

1. LuCI systime command injection — root RCE, authenticated admin (existing writeup; re-verified).
2. **New:** second injection in `apprpc/system.lua#setclock` — reachable authenticated via
   `/rpc/app` and **unauthenticated from the web's perspective via the cloud apprpc channel**.
3. MQTT `router/command` root shell gated only by the topic's `src_did` (prior chain re-confirmed).
4. **New:** MQTT `router/upgrade` shell injection via unquoted `url/md5/lang` fields.
5. `router/config` lets a controller rewrite arbitrary satellite UCI (auth = topic string only).
6. Predictable factory root password `sha256(fuuid+hmac)` from bdinfo on release images.
7. Hidden mesh AP PSK derivable from `md5(mac)/md5(hmac)` and leaked in `sysreport`.
8. Cloud apprpc proxy gives full `luci.app` RPC power (setpasswd/setclock/upgrade/…) with only a
   per-device client cert as the gate; no per-request signing.
9. `hcshd` network command daemon executes RSA-decrypted commands as root (dormant).
10. Broker: 1883 plaintext + 8883 mutual-TLS, both 0.0.0.0; JWT weaknesses (fixed exp floor, substring
    checks) and firmware-wide crypto constants (AES key, DES literal, sign salt — protocol constants,
    not per-unit credentials).

## Still opaque (and why)

- `hi_appm`/`libhi_*` handler internals (voice/PON/upgrade) — stripped, only IPC descriptor schema.
- WLAN driver ABI + HSLINK on-wire format — kernel modules; userspace surface decoded only.
- EasyMesh protocol internals — `libhi_emcore.so` (768 KB) not decompiled.
- Cudy cloud server behaviour — client side complete, server semantics inferred.
- `crypt -a/-g` exact KDF, factory bdinfo field generation, U-Boot env contents, TR-069 stack.
100% coverage plan: Ghidra the remaining `libhi_*`/`hi_appm`/`libhi_emcore`/`libcwmp` binaries,
then read-only dynamic confirmation (test broker, `/dev/hi_ipc` tracing, softapd↔driver tracing,
cloud capture on an owned device), plus the kernel RE already tracked in `docs/phase*`.

## Reproduce

See REPORT §0/§12: patched 32-bit Lua 5.1.5 + Python transcoder + unluac for the 323 Lua files;
cross `arm-none-linux-gnueabihf-objdump` + Ghidra headless for the stripped ARM binaries. Rootfs
is not committed; the pipeline needs only the extraction path above.

## Publication constraints honoured

Writes confined to this directory; no device/release/curated-repo writes; no vendor binaries, no
extracted DTS/firmware blobs; per-unit secrets (bdinfo `secret`/`hmac`/`fuuid`, MACs) not
reproduced — only firmware-wide protocol constants needed to understand the formats.
