# Opaque binaries — closing the vendor userspace/kernel-adjacent gaps

**Extends** `opensource/docs/incoming/vendor-userspace/REPORT.md` **§11 ("What is still
opaque")**. This file re-opens each §11 row, reports what was decoded, and gives a per-item
decoded-vs-opaque verdict. It does **not** redo §0–§10; it assumes them as prior art.

**Scope / method.** Host-only, read-only, static. Firmware `2.4.15-20251030-114751` (R116),
OpenWrt 22.03.6 / kernel 5.10.201, rootfs at `/home/shibbityshwab/recovery-extract`. Tools:
`arm-none-linux-gnueabihf-{objdump,readelf,nm,strings}` (notebook toolchain),
Ghidra 12.2 headless (Java 25, `DumpDecomp.java` decompiler dump; Jython is not installed, so
the dump script is Java). No device interaction; no vendor binaries/objects are committed — paths
refer to the local extraction. Per-unit values are redacted as `<secret>`, `<mac>`,
`<fuuid>`, `<hmac>`, `<sn>`, `<pin>`. Firmware-wide constants already published in the
parent report (DES key `88T3j05dtFu8=`, cmagent AES key `CLOUDmqttabc&123`, …) are repeated
only where they are inputs to a derivation documented here.

---

## 1. Priority 1 — hi_appm / hi_ipc / libhi_* command dispatch (largely DECODED)

### 1.1 Process architecture (decoded)

`/usr/bin/hi_app` (procd init) runs:

```
hi_appm -n sysdev -l sysinfo,board,encrypt,upgrade -s 0
```

`hi_appm` is a thin OWAL ("SSF") app runner: it links `libhi_owal_ssf.so`,
`libsecurec.so`, `libubus`, `libubox`, `libuci`, `libssl`, `libcrypto`. It has **no
handlers of its own**; all command handlers come from the four `-l` modules. `-c` stops it.

Module load (`owal_app_load`, Ghidra `0x12528` in libhi_owal_ssf.so): for each comma token
`M` in `-l`, it `dlopen("libhi_M.so", RTLD_NOW)` and `dlsym("hi_M_init")` then calls
`hi_M_init()`. The module init registers every command it implements via
`hi_owal_ipc_register` (string `hi_%s_init` is the dlsym template). Module → lib → entry:

| `-l` token | .so | entry | registered verb(s) |
|---|---|---|---|
| sysinfo | libhi_sysinfo.so | hi_sysinfo_init | `hi_sysinfo_data_get` |
| board | libhi_board.so | hi_board_init | `hi_board_info_get` |
| encrypt | libhi_encrypt.so | hi_encrypt_init | `hi_encrypt_rsa_de` |
| upgrade | libhi_upgrade.so | hi_upgrade_init | ~27 verbs (table below) |

### 1.2 /dev/hi_ipc transport message (decoded — exact layout)

`hi_ipc_sys_call()` (`libhi_owal_ssf.so` `0x13fa8`) is the raw transport. It builds one
message and sends it with `ioctl(fd, 0, msg)` on `/dev/hi_ipc` (opened `O_RDWR|O_NONBLOCK`,
mode 0600 — a **root-only** control channel; the char device is `hi_kipc.ko`):

```
struct hi_ipc_msg {                 // little-endian ARM
  char   name[0x100];   // 0x000  command/verb name, NUL-terminated (max 255)
  uint32 len;           // 0x100  length of the attribute blob
  uint32 ret;           // 0x104  return code, written back by the server
  uint8  data[];        // 0x108  serialized attribute blob (len bytes)
};
```

`ioctl` request number is **0**. Max `len` is `0xfffffef7` (the client rejects larger with
"ipc input data too big"). The response overwrites `ret` and (for read-style commands)
`data` in the same buffer. This is a fully-decoded ABI: any root process that can open
`/dev/hi_ipc` can invoke **any** registered verb by name; the name is an unauthenticated
string and there is no per-verb permission check on the ioctl path.

### 1.3 Dispatch (decoded)

`hi_owal_ipc_call(name, data, len)` (`libhi_owal_ssf.so` `0x13138`) is three-tier:

1. **Local handler** — walk the `hi_owal_ipc_register` list, `strcmp(node.name, name)`, call
   `node->callback(data, len)` (signature `int cb(void *data, uint len)`).
2. **System call** — if not local, `hi_ipc_sys_call(name,data,len)` (the ioctl transport above,
   which reaches *other* processes/apps — notably hi_appm for the sysdev app, but also any app
   registered in the kernel `hi_kipc` IPC db).
3. **ubus fallback** — if the sys-call returns a "not found" sentinel, `ubus_lookup_id(name)` +
   `ubus_invoke_fd(ctx, obj, name, blob, …)` — i.e. a verb name can resolve to a ubus object
   method of the same name (blobmsg payload).

So the "IPC bus" is actually **local-registry → /dev/hi_ipc ioctl → ubus**, unified by the verb
name string.

### 1.4 Descriptor ABI (/home/cmd) (decoded)

Descriptor files are the INI-like grammar already shown in the parent report:

```
[settings]  functionname = <verb>;  attributenum = N;  attributeflag = 0|1
[parameter] attribute = alias<key> type<T> range<lo,hi> default<d>;   (repeated)
[help]
```

`hi_ipc <desc> [-v k1 v1 k2 v2 …] | [-f N] | [-p] | [-s]` parses the descriptor
(`hi_ipc_cmd_parser`, `hi_ipc_cmd_script_open`), turns each `key=value` pair into a typed
attribute, then serializes and ioctl-sends it. Attribute **types** (strings in
libhi_ipc_cmd.so): `int`, `uint`, `hex`, `ipv4`, `ipv6`, `ipvX`, `mac`, `string`.
Serialization (`hi_ipc_cmd_vtype_value` `0x14338`): the blob is a **packed C-struct of
native-typed fields in declaration order**, each field aligned up to its natural size (fixed-size
types) or a per-type size callback (strings/variable). A 52-byte static type table maps type name
→ {size, alignment, set-value callback, size callback}. `range<lo,hi>` bounds the value;
`default<d>` supplies the value when `hi_ipc <desc>` is run with no `-v` args. Error codes
are `0x7220xxxx` (param/value/alloc/too-many-attrs) and `0x70000000` (generic) — same
HiSilicon SDK error domain used by every `hi_ipc_*` return.

### 1.5 Undocumented verbs (decoded — the "opaque" list is now enumerated)

The registered call-wrappers use the SDK convention `hi_hi_<verb>_call`. Enumerating every
`hi_hi_*_call` string in the loaded modules and subtracting the `/home/cmd/upgrade` descriptors
(`hi_upgrade_flag_get/set`, `hi_upgrade_image_write_file`,
`hi_upgrade_image_check_file`, `hi_upgrade_cmd`, `hi_upgrade_dump`, `hi_img_size`,
`dl_download_remote_file`, `dl_nc_upgrade_start`) yields the **verbs that are registered but
have no shipped descriptor**:

**libhi_upgrade.so** (the full set of ~27 wrappers):
`hi_hi_upgrade_flag_get/set`, `hi_hi_upgrade_image_write_file`,
`hi_hi_upgrade_image_check_file`, `hi_hi_upgrade_cmd`, `hi_hi_upgrade_dump`,
`hi_hi_upgrade_info_get`, `hi_hi_upgrade_image_write` (blocking whole-image, vs the
streaming `_file`), `hi_hi_upgrade_image_sharding_write`,
`hi_hi_upgrade_sharding_start/isdone`, `hi_hi_upgrade_block_start/write/isdone/stop`,
`hi_hi_upgrade_master_firmware_main`, `hi_hi_upgrade_master_firmware_monitor_main`,
`hi_hi_upg_get_img_size`, `hi_hi_upg_get_img_version`, `hi_hi_download_remote_file`,
`hi_hi_download_status_get`, `hi_hi_download_get_result`,
`hi_hi_download_thread_tag_set`, `hi_hi_nc_download_cmd`, `hi_hi_encrypt_rsa_de`,
`hi_hi_sysinfo_data_get`, and the standalone **`hi_IPC_igdCmOamCtrl`** (an OMCI/OAM gateway
control verb — the IGD "OamCtrl" entry, dormant on this router).

**Board / sysinfo / encrypt modules** register `hi_board_info_get`,
`hi_sysinfo_data_get`, `hi_encrypt_rsa_de` with **no** `/home/cmd` descriptor (the tree only
ships `upgrade/`, `pon/`, `voice/`). The **kernel** `hi_kipc.ko` additionally registers
`hi_kernel_ipc_db_dump` (its symbol `hi_hi_kernel_ipc_db_dump_call` + `g_ast_ipc_db`), so
the kernel module itself is an IPC peer with at least one debug verb.

### 1.6 Still opaque (and why)

- The **body** of each `hi_hi_*_call` wrapper (exact struct layouts per verb, MTD/sharding math
  inside `hi_upgrade_*`) is only partially reconstructed from strings; a full Ghidra pass with
  demo descriptor inputs is needed for the per-verb request/response layouts.
- `/home/cmd/pon/**` (51 descriptors) and `/home/cmd/voice/**` (150+ descriptors) are **dormant
  stubs**: the implementing modules (`hi_pon_*`, `hi_adpt_*`, `hi_slic_*`, `hi_voip_*`
  symbols) are **absent** from this router image — no `libhi_pon/voice/slic/voip` .so ships.
  Those verbs cannot run on a WR3000 (PON/voice gateway SDK leftovers). Their grammar/types are
  fully parseable but the server side does not exist locally.
- The `hi_kipc.ko` ioctl *server* internals (message routing between processes, the `g_ast_ipc_db`
  database) are kernel-side; only the client message format (§1.2) is derivable from userspace.

**Verdict: mostly DECODED** (dispatch, transport layout, verb enumeration). Residual: per-verb
payload layouts and kernel-side routing.

---

## 2. Priority 2 — HSLINK on-wire + softapd / HyFi surfaces (partially DECODED)

### 2.1 HSLINK = HiSilicon "Station Link" (ARP/RARP-based; decoded to the frame level)

`hi_khslink.ko` (21,508 B) strings + symbols fully characterize it. It is **not** a custom
EtherType protocol — it is an **ARP-family station-tracking/link-state mechanism** hooked into the
CFE (Central Forwarding Engine):

- Frames: `barp` (broadcast ARP), `rarp` (reverse ARP), `urarp` (unicast reverse ARP),
  sent periodically (`hi_hslink_send_*`, `hi_hslink_tx_{broadcast,urarp}`,
  `hi_hslink_set_urarp_smac`), with `eth_type_trans` at L2 and `hi_cfe_get_igr_port` for the
  ingress port.
- State: an ARP table (`hi_hslink_arp_tab_{init,exit,add,handle,dump,aging}`,
  `hi_hslink_arp_info_{clr,get_cnt}`, `hi_hslink_add_arp_tab`) keyed by IP/MAC
  (`hi_hslink_get_ip_by_mac`, `hi_hslink_cmp_ip/dev`).
- "Drift" detection = station movement/roaming (`hi_hslink_drift_del_table`,
  `hi_hslink_dfx_drift_del_en_*`), plus DFX debug knobs
  (`hi_hslink_dfx_{arp_age_en,period,send_times,urarp_smac}_{get,set}`).
- Link events via `hi_hslink_dev_link_notify` + notifier callbacks.

So the HSLINK "on-wire format" is **standard ARP/RARP frames** (with a vendor-chosen
broadcast/unicast S-MAC policy), not a bespoke header. The QCA "HyFi bridge" sits **above** it as
the L2 relay/forwarding layer (§2.2), and `hi_swa.ko` is the switchdev port-object layer
(`switchdev_handle_port_obj_{add,del}`, `hi_net_get_port`) that attaches the bridge to
physical/WiFi ports. `hi_woc_wifi_rtx.ko` (`hi_wifi_rx_pkt`) feeds WiFi RX into the same path.

### 2.2 HyFi bridge userspace (decoded — full API surface)

`libhyfi-bridge.so` is a QCA-derived HyFi bridge client (function names are the Qualcomm
`bridge*` API, compiled for HiSilicon): `bridgeAttach/Detach`, `bridgeSetBridgeMode`
(`relovr`/GRPREL), `bridgeSetForwardingMode` (`APS/SINGLE/MCAST/ISOLATE`),
`bridgeSetIFType` (port classes `w2|w5|plc|eth|w6|mld`), `bridgeSetIFGroup`,
`bridgeSetIFBroadcast`, `bridgeSetTcpSP`, `bridgeSetHATable*Entries/AggrEntry/AgingParams`,
`bridgeAdd/DelHDTableEntries`, `bridgeSetSnoopingParam` (IGMP/MLD), `bridgeSetServicePrioritizationRuleSet`,
`bridgeSetPathSwitch{Param,AdvancedParam,SwitchEndMarkerTimeout}`, `bridgeSetColocatedIface`,
`bridgeSetMAPOperatingMode`, `bridgeSetBridgeMapTrafficSeparationMode`, `bridgeSetMSCSRule`,
`bridgeSetSCSRule`, `bridgeToggleLocalBit`, `bridgeGetVersionCompatibility`,
`bridgeGetLANPortNumber`, `bridgeGetFourAddressIface`, plus `HYFI_STATUS_*` result codes.
MSCS/SCS/MAP = Wi-Fi Alliance EasyMesh QoS/operating-mode verbs — this is the mesh L2 data-plane
glue.

`hyfibrd` = ubus object `hyfibr` with methods `init/setgrp/setbcast/ports/bstevt/debug`
(confirmed in strings), driving `bridgeSet*`. `hyctl` = the full CLI (usage strings recovered):
`attach/detach`, `setbrmode relovr|grprel`, `setfwmode APS|SINGLE|MCAST`,
`setiftype w2|w5|plc|eth|w6|mld`, `setifgrp`, `setifbcast`, `sethatbl/gethatbl`,
`gethdtbl/addhdtbl`, `setaggr`, `sethatblage`, `setmc` (IGMP/MLD snooping ACL/aging),
`settcpsp`, plus `show`/table dump.

`hcshd` (9 KB) — a dormant UDP **root-command receiver** that RSA-decrypts each datagram with an
embedded 1024-bit public key before `system()/popen()`; it binds late (S98) and no shipped
script feeds it. (Prior report; no new ports/keys discovered — still dormant.)

### 2.3 softapd / WLAN driver ABI (decoded one layer deeper)

`softapd` (230 KB) is the `wireless` ubus object (`up/down/notify/smt/smton/smtoff/wps*/wds*/
roaming/aclchk/debug/cleanup`) but its **driver transport is now pinned**: it uses a vendor
generic-netlink family via `nlwifi_*` (`nlwifi_hi_wifi_init`, `nlwifi_hi_wifi_rcv_msg_func`,
`nlwifi_set/get_*`) — the Hi5622 `hsanwifi` netlink ABI, **not** nl80211 — plus legacy
`iwpriv` (`ap_chan_scan`, `set_ie`, `set_priv_scan`, `11k_set_neigh`, `kick_user`,
`usernum`, `bintval`, `dtim_period`, `resp_hide`) and a sysfs `/sys/hisys/hipriv` for
`wds_sta_del`. SMT = an IE carrying SSID/auth/PSK (`smt_ap_start`, `smt get ssid/auth/psk`,
default PSK `CUDYMeshSMT_1234`). WDS roaming/11k/11v (`nlwifi_set_11k_neighbor`,
`nlwifi_tx_11v_bstm`), TWT, ACL, and key ops (`nlwifi_set_gtk/ptk/auth`) are all surfaced.
`softapd` also reads bdinfo (`bdinfo_get_value`) and does RSA/BN via libcrypto.

### 2.4 Still opaque (and why)

- The **byte layout** of the `nlwifi` generic-netlink messages (attribute IDs, the `set_priv_scan`
  "1 1 60 2 1 1 6 110 1" parameter vector, key encodings) lives in the `hsanwifi` driver module
  — kernel-side, not in this rootfs.
- The exact ARP/RARP **distinguishing fields** (which opcode/S-MAC bit marks `barp` vs `rarp`
  vs `urarp`) are in `hi_khslink.ko` code, not strings; needs kernel disasm (tracked in the
  notebook kernel phases).
- SMT IE internal TLV layout is only partially visible (SSID/auth/PSK extraction is in softapd
  code paths not fully decompiled here).

**Verdict: mostly DECODED** (HSLINK frame family, HyFi full API + hyctl surface, softapd transport
layers). Residual: netlink/ARP byte formats and kernel internals.

---

## 3. Priority 3 — crypt -a/-g KDF and the cmagent "crypt list" (DECODED)

### 3.1 `/usr/bin/crypt` key derivation (pinned — exact)

`crypt` (`main` `FUN_000109c0`, KDF `FUN_00011a28` `0x11a28`) is AES-CBC (OpenSSL
`AES_*_cbc_encrypt`). CLI: `crypt [-e|-d] [-a|-g|-n] -l bits -k key -v iv -i in -o out`.
Key mode is selected by `-a` (mode 2), `-g` (mode 1), `-n` (mode 3), or neither (mode 0).
For `-a`/`-g`/default the key is **AES-128 = MD5(material)**, IV all-zero, PKCS#7 padding,
hex-encoded to stdout (raw to `-o` file). Exact KDFs (decompiled + string-resolved):

| mode | key material | key |
|---|---|---|
| `-a` (standard) | `"<board>@aes#hex"` | MD5 → AES-128 |
| `-g` (global) | `"@" + <secret>` | MD5 → AES-128 |
| default (`-e/-d` alone) | `"<hostname>@<secret>"` | MD5 → AES-128 |
| `-n` (explicit) | `-k` hex key, `-v` hex iv | direct AES-set-key (128/192/256) |

where `<board>` = `bdinfo_get_value("board", …)`, `<secret>` = `bdinfo_get_value("secret", …)`,
`<hostname>` = `gethostname()`. (This **corrects** the parent report §8, which said `-g`
derives from `board`: it is `-a` that uses `board`; `-g` uses `secret`.)

Consumers confirm the split: `crypt -ea` (board key) stores the LuCI/TR-069 password hash;
`crypt -gd` (secret key) decrypts the shipped mosquitto mutual-TLS certs at every boot;
`crypt -e -o` (hostname@secret) encrypts TR-069 backups and `cmsd-control` cloud certs.

**Implication (flag for reconciliation):** `crypt -gd` decrypts *firmware-wide* cert files with
no per-unit re-encryption step, so the bdinfo `secret` must be a **fleet-wide shared value** (it
is also the mosquitto JWT HS256 key that any mesh peer must share). This contradicts the parent
report's §1 classification of `secret` as per-unit; `hmac`/`fuuid`/`mac`/`sn` remain
per-unit.

### 3.2 cmagent "crypt list" AES channel (confirmed)

The cmagent obfuscation channel key is the 16-byte literal `CLOUDmqttabc&123` (confirmed present
verbatim in `usr/sbin/cmagent`), AES-128-CBC, **all-zero IV**, PKCS#7 to 16 bytes. The
per-peer "crypt list" state machine (add on first non-JSON traffic, count, confirm/delete;
`000000000000` controller exempted) is exactly as the parent report §4.3/§4.4 describes. No new
key material found. This is obfuscation, not authentication.

### 3.3 libhi_encrypt.so — the IPC "encrypt" module primitives (decoded)

Separate from the `crypt` CLI, the loaded `encrypt` module exposes via IPC:
`hi_encrypt_aes_cbc_en/de`, `hi_encrypt_aes_str_en/de`, `hi_encrypt_enfile/defile`,
`hi_encrypt_file_zip/unzip`, `hi_encrypt_pbkdf2_en`, `hi_encrypt_rsa_de`, `hi_encrypt_dh`,
`hi_encrypt_sha256_image`, `__rootkey_gen`, `hi_rand_bytes`. Pinned primitives:

- **PBKDF2** (`hi_encrypt_pbkdf2_en` `0x127bc`): `PKCS5_PBKDF2_HMAC(EVP_sha256)`;
  struct {salt, saltlen (8<len≤128), salt_flag (0=random RAND_bytes), iter (>1000),
  password, pwlen, out, keylen (≥16)}. So PBKDF2-HMAC-SHA256, random-or-supplied salt.
- **RSA** (`hi_encrypt_rsa_de` `0x128a0`): `RSA_public_decrypt(pad=PKCS#1 v1.5)` with an
  embedded PEM public key; input ≤ 0x180 (384) bytes.
- **DH** (`hi_encrypt_dh` `0x129cc`): fixed group loaded by `BN_hex2bn` (p and g as hex
  literals), 767-bit random exponent (`BN_rand(…,0x2ff,…)`), `g^rnd mod p`, hex output.
- **Root key gen** (`__rootkey_gen` `0x137d8`): AES-encrypt an input string
  (`hi_encrypt_aes_str_en`) → 16-byte intermediate; XOR three 128-byte `hi_rand_bytes` buffers
  into a 128-byte password; `hi_encrypt_pbkdf2_en` (iter=10000, keylen=16) → 16-byte root key.

### 3.4 Still opaque (and why)

The **inputs** to `__rootkey_gen`/DH/RSA (which string, which group hex) come from runtime
callers of the encrypt IPC module that are not fully traced in this pass, and the per-unit bdinfo
values that feed `crypt -g/-a` are device state, not in the image.

**Verdict: DECODED** (all KDF formulas exact, plus the extra IPC crypto module).

---

## 4. Priority 4 — bdinfo field generation and factory layout (partially DECODED)

### 4.1 Field set + fallbacks (decoded)

Documented field consumers across scripts + `lib/functions/bdinfo.sh` defaults:

| field | consumer / default |
|---|---|
| mac | LAN MAC (`get_bdinfo_mac`, no default) |
| board | `get_bdinfo_board` → fallback `system_board_name` |
| model | → fallback `UN0` |
| country | → fallback `CN` |
| sn, pin, secret, hmac, fuuid, factory, region, rdtest, dbg, deviceid | bdinfo-only (no shell default) |
| private_key / public_key / endpoint_host / endpoint_port / addresses | WireGuard mesh (optional) |
| automesh | mesh enable flag |

The partition format itself (0xDE96 bytes: BE version u32, DES-CBC payload `88T3j05dtFu8=`,
RSA-1024 MD5 sig at 0xDD80, raw 6-byte MAC at 0xDE00, second 0x80 sig slot at 0xDE16) is already
fully documented in the parent §6.4 and is not re-derived here.

### 4.2 KDF ties (decoded — new)

`crypt -a` consumes `board`, `crypt -g` consumes `secret` (§3.1); the `bdinfo dbg` gate is
`hex(SHA256(<flash_uuid> + <hmac> + "@2025"))` (format string `%s%s@2025` confirmed in
libbdinfo.so), and `bdinfo checkuuid` binds `fuuid` to `/proc/sys/dev/flash_uuid`. These pin
*which* field feeds *which* derived value.

### 4.3 Still opaque (and why)

**Generation** of `hmac`/`sn`/`pin`/`region`/`secret`/`fuuid` at the factory is
**not** in the rootfs: the writers are factory tooling (the `bdinfo sign <b64>` CLI only writes a
provisioned signature; the DES/RSA private material and the field formulas are on the
manufacturing line, not the shipping image). The factory partition *layout* is decoded; the
*provisioning algorithm* is not locally observable. `secret` being fleet-wide (§3.1) means it is
a shipped constant, not generated per-unit.

**Verdict: mostly DECODED** (field set, defaults, derivation consumers, partition layout).
Residual: factory-time field generation algorithm (absent from image).

---

## 5. Priority 5 — TR-069 / remote-management stack (DECODED to the request surface)

### 5.1 Components (decoded)

`cwmp` (175 KB, the daemon), `cwmp_cli` (9 KB CLI), `libcwmp.so` (256 KB data-model impl),
`/usr/bin/tr069/*` (RPC scripts + Lua helpers), `/etc/config/cwmp`, `/etc/init.d/cwmp`
(S99, Router mode only), `/etc/tr098.xml` + `/etc/tr181.xml` data models,
`/etc/nftables.d/.../30-cwmp.nft` (WAN input allow for the connection-request port).

### 5.2 Request surface (decoded)

- **Connection request**: `conn_reqport 7547`, `conn_reqpath /tr069`,
  `conn_username/conn_password = admin/admin` (default, disabled `enable off`),
  URL template `http://%s:%d%s`; UCI keys `CWMP_URL/CWMP_UN/CWMP_PW/CWMP_CR_UN/CWMP_CR_PW/
  CWMP_PIFI/ECWMP`.
- **Data model** (libcwmp.so symbols): TR-098 root `InternetGatewayDevice.WANDevice.1.
  WANConnectionDevice.1.{WANIPConnection,WANPPPConnection}.1.*` plus DeviceInfo, DeviceSummary,
  Ethernet (Link/Interface), Bridging (Bridge/VLAN), DHCPv4 Client/Server, PPP Interface,
  Cellular, NAT/PortMapping, IP Diagnostics (Download/Upload, full `IP_Diagnostics_*` object),
  ManagementServer (`EnableCWMP/PeriodicInform/ConnectionRequest*/UDPConnectionRequestAddress`),
  Layer3Forwarding, Wi-Fi/SSID/Host objects. Data model selectable `TR098`/`TR181` (config).
- **RPC scripts** (`/usr/bin/tr069/`): `cwmp-bkup` (Upload: `sysupgrade -b - | crypt -e -o`),
  `cwmp-restore` (Download config), `cwmp-upgrd` (Download firmware → `sysupgrade` after
  `sysupgrade -T` + supported_devices check), `cwmp-user` (SetParameterValues → LuCI password
  via `crypt -ea`), `cwmp-ddns/ntp/route/portfwd/qos/ppp/nat`, `cwmp_external_func`
  (customize hook, `option43_support`, `extra_script`), and Lua `verify_pms` (Parameter
  Management System set-value validator) + `diagnose` + `gen_hosts/gen_meshlist/
  gen_ethernet_port_info`.
- `cwmp_cli` = ubus/uhttpd-free local CLI (same libcwmp data model).

### 5.3 Still opaque (and why)

ACS URL/credentials come from DHCP option 43 or the vendor ACS (not in rootfs); the SOAP/HTTP
Inform session transcript and the ACS's exact response semantics require a live (authorized) ACS
session or a vendor API doc. libcwmp.so internals (parameter map ↔ UCI bridge, `verify_pms`
binding) are symbol-level only.

**Verdict: mostly DECODED** (full request/RPC surface, data model, config). Residual: ACS-side
session semantics.

---

## 6. Priority 6 — remaining firmware-wide protocol constants (DECODED)

Consolidated constants (all firmware-wide, none per-unit; reproduced as protocol constants):

| constant | value | where |
|---|---|---|
| cmagent AES key | `CLOUDmqttabc&123` | usr/sbin/cmagent (AES-128-CBC, IV=0) |
| bdinfo DES key | `88T3j05dtFu8=` | libbdinfo.so (DES-CBC, IV=0) |
| bdinfo dbg salt | `%s%s@2025` = SHA256(flash_uuid+hmac+"@2025") | libbdinfo.so |
| crypt -a salt | `%s@aes#hex` | usr/bin/crypt |
| crypt -g salt | `@%s` (secret) | usr/bin/crypt |
| crypt default | `%s@%s` (hostname@secret) | usr/bin/crypt |
| SMT mesh PSK | `CUDYMeshSMT_1234` | softapd |
| mesh controller id | `000000000000` | cmagent topics |
| cloud endpoints | `cn-api`, `cn-api-test`, `i18n`, `i18n-test` (`.cudycloud.com`, + `event-` prefix) | autoupgrade/cmsd-control |
| cloud sign salt | `md5(fuuid+mac+hmac+dtime+<salt>)` | autoupgrade/cmsd-control (salt in parent §5.1) |
| mosquitto roles | `cudydevice` / `admincudydevice` | auth_plugin_jwt + aclfile |
| HiSilicon IPC error domain | `0x70000000` generic, `0x7220xxxx` command/param/value/alloc | all libhi_ipc |
| HiSilicon IPC msg | name[0x100] + u32 len + u32 ret + data | /dev/hi_ipc ioctl(0) |
| OWAL module convention | `libhi_%s.so` + `hi_%s_init` | libhi_owal_ssf.so |

No additional undocumented secret material was found beyond what the parent report already lists.

**Verdict: DECODED.**

---

## 7. Per-item verdict summary

| §11 row | verdict | what closed it |
|---|---|---|
| hi_appm handler implementations | **mostly decoded** | dispatch tiers, /dev/hi_ipc msg layout, module-load convention, full verb enumeration incl. dormant pon/voice |
| WLAN driver ABI | **one layer deeper** | softapd transport pinned to vendor netlink (`nlwifi_*`) + iwpriv + /sys/hisys/hipriv; byte layout still kernel-side |
| HSLINK packet format | **frame family decoded** | ARP-family (barp/rarp/urarp) + CFE hooks + drift/aging; exact distinguishing bits still kernel-side |
| EasyMesh protocol | not re-derived here (out of scope for this pass) | parent §11 row; libhi_emcore (768 KB) still undecompiled |
| Cudy cloud server behaviour | **still opaque** | client-only; needs authorized cloud session |
| crypt -a/-g KDF | **decoded (exact)** | MD5(material) formulas + consumers + fleet-wide-secret implication |
| U-Boot env / A-B boot | not re-derived | device state, needs fw_printenv (read-only) |
| bdinfo field generation | **layout decoded, generation opaque** | field set/defaults/consumers pinned; factory writers absent |
| TR-069 | **mostly decoded** | full request/RPC surface + data model + scripts |
| Extracted DTS / board data | N/A | not in rootfs by design |

---

## 8. What remains genuinely opaque (and why)

1. **Vendor cloud server semantics** — endpoints/fields are known; auth/response/redirect behaviour
   needs an authorized capture (`*.cudycloud.com`).
2. **Factory-time bdinfo generation** — provisioning algorithm and RSA/DES private material live
   on the manufacturing line, not in the image.
3. **Kernel-side byte formats** — `hsanwifi` netlink attribute IDs, `hi_khslink` ARP
   distinguishing bits, `hi_kipc.ko` routing — all need kernel disasm (the notebook kernel
   phases), not derivable from this rootfs.
4. **Per-verb payload structs** — the 27 upgrade/board/sysinfo/encrypt `hi_hi_*_call` wrappers'
   exact request/response layouts need a full Ghidra pass with demo descriptor inputs.
5. **libhi_emcore.so (768 KB)** — EasyMesh core not decompiled in this pass.

Evidence / reproduction: decompiler dump at `/tmp/opaque-re/ghidra-out/*.c` (Java `DumpDecomp.java`
`-postScript`); symbol/string dumps at `/tmp/opaque-re/dynsyms.txt`; binary paths refer to
`/home/shibbityshwab/recovery-extract`. All findings are static against firmware 2.4.15; no
device was touched.
