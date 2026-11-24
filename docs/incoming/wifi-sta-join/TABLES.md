# TABLES - wifi-sta-join (task-146)

Derived facts only; every row can be regenerated with
`tools/fw_sta_join_re.py <pinned local vendor .ko>` (never committed).
Vendor `.ko` offsets are link-time `.text` section offsets.
Evidence markers: [V] re-derived by this lane, [V*] earlier attended bench,
[I] inference, [U] unknown.

---

## T1 - Pinned inputs and symbol anchors

| item | value |
| --- | --- |
| vendor module (local only) | 2.5.24 `hi5622v100_wifi.ko`, 3,564,728 B, sha256 `de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521` |
| firmware image (reference only) | `hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b` |
| target kernel | luofu 6.18.60, VMSPLIT_3G, `PAGE_OFFSET=0xC0000000`, `CONFIG_ARM_PATCH_PHYS_VIRT=y` |
| DTS dma-ranges | `0x02000000 0x0 0x80000000 0x80000000 0x0 0x10000000` (identity) |
| host-DMA window | `0x80000000..0x8fffffff` (`HI5622_HOST_DMA_LO/HI`) |

| symbol | `.text` off | size | role |
| --- | ---: | ---: | --- |
| `oal_cfg80211_disconnected` | 0x00077c | 0x28 | wrapper -> `cfg80211_disconnected` |
| `oal_cfg80211_connect_result` | 0x000888 | 0x98 | wrapper -> `cfg80211_connect_done` |
| `wal_cfg80211_connect` | 0x100590 | 0x2b4 | cfg80211 .connect: builds 108-B body |
| `wal_cfg80211_disconnect` | 0x100844 | 0x1b8 | cfg80211 .disconnect |
| `wal_main_init` | 0x123cac | 0x150 | registers WAL chip-group-2 table |
| `wal_asoc_comp_proc_sta` | 0x125b6c | 0x2d0 | D2H g2 id3 assoc completion [V] |
| `wal_disasoc_comp_proc_sta` | 0x125e3c | 0x124 | D2H g2 id4 disassoc completion [V] |
| `wal_send_cfg_event` | 0x1270d8 | 0x474 | WAL cfg-event sender |
| `wal_cfg80211_start_req` | 0x127ec4 | 0x1d0 | `{u16 opcode,u16 len,body}` framing |
| `wal_cfg80211_start_connect` | 0x128310 | 0x24 | opcode 0x123, len 0x6c, wait=1 |
| `wal_cfg80211_start_disconnect` | 0x128334 | 0xbc | opcode 0x0d0, len 0x10, wait=1 |

## T2 - WAL cfg-event frame envelope [V]

| off | width | connect | disconnect |
| ---: | ---: | --- | --- |
| +0x00 | u8 | group nibble 2 | 2 |
| +0x01 | u8 | `0x01 | (dst<<4)` | same |
| +0x04 | u16 | total 140 | total 48 |
| +0x06 | u16 | HCC id 0 | 0 |
| +0x0a | u16 | tag 0x5a5a | 0x5a5a |
| +0x0c | u32 | WAL cookie 0 | 0 |
| +0x10 | u32 | WAL msg class 1 | 1 |
| +0x14 | u16 | pad 0 | 0 |
| +0x16 | u16 | data_len 112 | 20 |
| +0x18 | u32 | sequence | sequence |
| +0x1c | u16 | opcode 0x0123 | 0x00d0 |
| +0x1e | u16 | body_len 108 | 16 |
| +0x20 | - | body (T3) | body (T10) |

Bench-proven carrier: the AP-start lane's 4 WAL frames use the same
12-byte HCC header + WAL envelope and are consumed by the firmware [V*].
The inherited fail-closed WAL allowlist already carries `{0x0123,108}`
(sta-static lane) and `{0x00d0,16}`; this lane adds no wire change.

## T3 - Connect body (108 B) field map [V]

Body built at vendor `sp+0x18`, memset 108 (`mov r3,#0x6c` @0x1005ec).
Source column = 5.10 `struct cfg80211_connect_params` offset.

| off | width | field | vendor store | source |
| ---: | ---: | --- | --- | ---: |
| +0x00 | u8 | reserved 0 | memset | - |
| +0x01 | u8 | ssid_len | `strb r3,[sp,#0x19]` @0x100618 | params+0x14 |
| +0x02 | u8 | mfp | `strb r3,[sp,#0x1a]` @0x100644 | params+0x28 |
| +0x03 | u8 | 0 | memset | - |
| +0x04 | u32 | IE address (raw pointer) | `str r2,[sp,#0x1c]` @0x100680 | params+0x1c |
| +0x08 | u32 | SSID address (raw pointer) | `str r3,[sp,#0x20]` @0x100600 | params+0x10 |
| +0x0c | u32 | BSSID address (raw pointer) | `str r3,[sp,#0x24]` @0x10061c | params+0x08 |
| +0x10 | u8 | privacy | `strb r3,[sp,#0x28]` @0x100638 | params+0x24 |
| +0x11 | u8 | auth_type | `strb r3,[sp,#0x29]` @0x10062c | params+0x18 |
| +0x12 | u8 | key_len | `strb r7,[sp,#0x2a]` @0x1006d4 | params+0x78 |
| +0x13 | u8 | key_idx | `strb r3,[sp,#0x2b]` @0x1006dc | params+0x79 |
| +0x14 | u32 | key address (raw pointer) | `str r2,[sp,#0x2c]` @0x1006d8 | params+0x74 |
| +0x18..+0x67 | - | 0 | memset | - |
| +0x68 | u32 | ie_len | `str r3,[sp,#0x80]` @0x100684 | params+0x20 |

There is **no channel/frequency and no HT bandwidth field** in the body [V].
The four addresses are dereferenced by the firmware; on the vendor kernel
(VMSPLIT_2G) raw kmalloc VAs equal device addresses. Our VMSPLIT_3G kernel
must pass device addresses in the host-DMA window.

## T4 - Body address validation (hardened policy)

Buffer (one `dma_alloc_coherent` allocation, 584 B):

| region | offset | size | must be |
| --- | ---: | ---: | --- |
| bssid | 0 | 6 (+2 pad) | valid unicast BSSID (caller), non-zero |
| ssid | 8 | 32 | 1..32 B |
| ies | 40 | 512 | 0..512 B |
| key | 552 | 32 | 0..32 B |

Rules enforced before a frame exists
(`hi5622_sta_join_bufs_check()`):

| rule | refusal |
| --- | --- |
| all four addresses non-zero | -EINVAL |
| 4-byte aligned | -EINVAL |
| inside `0x80000000..0x8fffffff` | -ERANGE |
| whole buffer inside the window | -ERANGE |
| fixed layout (buf+0/+8/+40/+552) | -EINVAL |
| `ssid_len 1..32`, `ie_len<=512`, `key_len<=32`, `key_idx<=3`, auth 0..8 | -EINVAL/-E2BIG |
| each region inside the buffer, no u32 wrap | -ERANGE |
| caller buffer size >= 584 | -ENOSPC |

Post-build re-parse (`hi5622_sta_connect_body_check()`): reserved bytes 0,
every pointer/len equals the validated bufs.

## T5 - Join gate matrix (`hi5622_sta_join_gate`)

| sta_join | allow_frames | already_attempted | result |
| ---: | ---: | ---: | --- |
| 0 | any | any | -EOPNOTSUPP (default fail closed) |
| >2 | any | any | -EINVAL |
| 1 | any | any | proceed dry (build+validate+log, no post) |
| 2 | 0 | any | -EPERM |
| 2 | >0 | 1 | -EALREADY (one join per load) |
| 2 | >0 | 0 | proceed post |

Latch is set **before** the device write; every post failure is final for
the load. Disconnect: own `disconnect_attempted` one-shot latch;
`-ENOTCONN` when no join was posted.

## T6 - Health stop witnesses ("mask"/"staging") [V]

| bench read-pack rule | firmware CPU | project constant | driver witness |
| --- | ---: | --- | --- |
| page golden `0x86807fef/7/2/0xabcd` | 0x001bf000 | `HCC_CPU_HANDSHAKE_PAGE` | boot latch (frame_post) |
| state = 1 | 0x00118b60 | `HCC_CPU_BOOT_STATE` | boot/phase gates |
| **mask = 0** | **0x00040008** | `HCC_CPU_EXCP_MASK` (exception-seen bitmap) | `core->exception_mask` (latched) |
| **staging = 0** | **0x0004000c** | `HCC_CPU_VECTOR_FRAME` (17-word vector context) | `core->vector_capture_count` |

Non-zero mask or any captured vector frame -> `hi5622_sta_health()` returns
-EIO and the join is refused (dry or post): STOP, no retry, power-cycle if
nonzero.

## T7 - D2H WAL chip-group-2 dispatch table [V]

Registration: `wal_main_init` `.text+0x123cac` ->
`hcc_msg_register_tab_chip(2, &table, 0x13)` at `.text+0x123ce0`
(`mov r2,#0x13` @0x123cd0, `mov r0,#2` @0x123cdc). Table at
`.data+0x1c74`, 19 entries of 12 B `{u32 id; handler; u32 arg}`.

| id | handler | id | handler |
| ---: | --- | ---: | --- |
| 0 | `wal_config_process_pkt` | 13 | `wal_cfg80211_mgmt_tx_status` |
| 1 | `wal_acs_netlink_recv_handle` | 20 | `wal_cfg80211_cac_report` |
| 2 | `wal_scan_comp_proc_sta` | 21 | `wal_receive_all_sta_rssi_proc` |
| **3** | **`wal_asoc_comp_proc_sta`** | 30 | `wal_report_external_auth_req` |
| **4** | **`wal_disasoc_comp_proc_sta`** | 31 | `wal_process_packet_xmit` |
| 5 | `wal_connect_new_sta_proc_ap` | 32 | `wal_dfr_power_down_dev` |
| 6 | `wal_disconnect_sta_proc_ap` | 37 | `wal_multiap_report_proc` |
| 7 | `wal_mic_failure_proc` | 38 | `wal_hiwifi_report_proc` |
| 8 | `wal_acs_response_event_handler` | | |
| 9 | `wal_send_mgmt_to_host` | | |
| 12 | `wal_cfg80211_init_evt_handle` | | |

## T8 - D2H assoc-completion (g2 id3) payload map [V]

Wire = 12-byte HCC header + payload. Payload base = wire offset 12
(`r6+0x0c` with `r6 = *(msg+0x118)`).

| payload off | width | field | vendor handler evidence (`.text`) |
| ---: | ---: | --- | --- |
| +0x00 | u16 | [U] not read | - |
| +0x02 | u16 | status (0 = success) | `ldrh r1,[r6,#0xe]` @0x125bfc |
| +0x04 | u8[6] | bssid | `add r2,r6,#0x10` @0x125bd4 + `memcpy_s` @0x125be0 |
| +0x0a | u16 | [U] not read | - |
| +0x0c | u32 | req_ie_len | `ldr r3,[r6,#0x18]` @0x125be8 |
| +0x10 | u32 | resp_ie_len | `ldr ip,[r6,#0x1c]` @0x125bec |
| +0x14 | u32 | req_ie host-DRAM device address | `ldr r2,[r6,#0x20]` @0x125be4 |
| +0x18 | u32 | resp_ie host-DRAM device address | `ldr r0,[r6,#0x24]` @0x125bf8 |
| - | - | success path BSS update | `bl wal_update_bss` @0x125c60 |
| - | - | completion | `bl oal_cfg80211_connect_result` @0x125c8c |

Min payload length 0x1c (28 B). The two IE addresses are **untrusted
firmware input**: log-only, window/alignment/length validated, never
dereferenced by this lane.

## T9 - oal wrapper -> cfg80211 struct mapping [V]

`oal_cfg80211_connect_result` (`.text+0x888`) builds 5.10
`struct cfg80211_connect_resp_params` at `sp+4` and calls
`cfg80211_connect_done` (reloc @`.text+0x8f0`):

| params field | 5.10 off | wrapper arg |
| --- | ---: | --- |
| status | +0x00 | arg7 (u16, from handler status) |
| bssid | +0x04 | arg2 (handler local struct = 6-B bssid) |
| bss | +0x08 | 0 |
| req_ie | +0x0c | arg3 |
| req_ie_len | +0x10 | arg4 |
| resp_ie | +0x14 | arg5 |
| resp_ie_len | +0x18 | arg6 |
| gfp | call arg3 | 0xa20 vendor constant |

## T10 - Disconnect body (16 B) + frame [V]

| off | width | field |
| ---: | ---: | --- |
| +0x00 | u8[6] | bssid |
| +0x06 | u16 | reason code (cfg80211 reason) |
| +0x08..+0x0e | - | 0 |
| +0x0f | u8 | 1 (vendor constant: `strb r3,[r4,#0xf]` @0x128360) |

Frame total 48 B, opcode 0x00d0, body_len 16.

D2H disassoc completion (g2 id4): payload +0x00 u16 reason
(`ldrh r1,[r4,#0xc]` @0x125ea0; ie=NULL/len=0)
-> `oal_cfg80211_disconnected` @0x125eb8 -> `cfg80211_disconnected`
(reloc @0x794).

## T11 - Implementation / verification identity

| item | value |
| --- | --- |
| patch | `patches/0001-sta-join-hardening.patch`, 8 files, applies on all-patch integration (`008aca5` chain) |
| module params | **47** (unchanged; `sta_join` now 0444 read-only) |
| stripped artifact | 314,244 B sha256 `e4e6118418f87bdb2a67395508fb104ad0dc789034a50ce2e1d9a7e7eac6cca6` (local only, not committed) |
| unstripped | 1,156,840 B sha256 `3c5cfb80abec3727706e1ed48a0e3ddda8cd77598a061da15ee055184581b7ac` |
| module metadata | `depends=cfg80211`, `vermagic 6.18.60 SMP mod_unload ARMv7 p2v8`; strip preserves `.text` |
| build | zero warnings/errors on 6.18.60; deterministic across two clean builds |
| host tests | 111 checks PASS |
| static re-derivation | 86 checks PASS |
| safety audit | 23 checks PASS |
| bench | **not fired** (host-only lane; ladder B0-B4 designed) |

## T12 - Evidence files

| file | content |
| --- | --- |
| `evidence/build-verify.txt` | full build-script output (patch apply, build, strip, assertions, selftests) |
| `evidence/build.log` | kbuild log (warning-free) |
| `evidence/artifact-identity.txt` | artifact size/sha256/params/patch hash |
| `evidence/selftest.txt` | host unit tests + re-derivation + audit |
| `evidence/sta-join-re-2.5.24.txt` | 86-check re-derivation from the pinned local vendor module |

## T13 - Corrections to earlier records

| # | correction |
| ---: | --- |
| C1 | The body's four addresses are raw cfg80211 params pointers [V]; work on the vendor build only because VMSPLIT_2G makes VA == device host-DRAM address. Our VMSPLIT_3G kernel must pass dma device addresses; a raw pointer is out-of-window and is now refused. |
| C2 | Zero is **not** a legal "absent" address sentinel [U] -> hardened builder always emits a valid zeroed region, absence carried by the length. |
| C3 | "One join per load, no retry" is now driver-enforced (previous lane allowed repeat posts). |
| C4 | The "mask"/"staging" bench stop rules map to firmware CPU 0x400008 / 0x40000c and are now a driver gate. |
| C5 | The D2H assoc completion is the WAL chip-group-2 **id 3** family (dispatch table + payload map in T7/T8), not an H2D/d2h-direct family; its two IE addresses are firmware-supplied host-DRAM addresses that must be treated as untrusted. |
| C6 | The sta-static open item "D2H management/assoc event decode" is closed at the static level; only the cfg80211 completion wiring remains (RX/AP-mgmt lanes). |
