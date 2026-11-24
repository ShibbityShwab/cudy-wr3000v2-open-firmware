# STA join path hardening + WAL frame / D2H completion verification

**Lane:** `wifi-sta-join` (shared task **task-146**, host-only, no bench
contact). **Written:** 2026-11-24. **Parent lane:** `wifi-sta-static`
(task-141) whose fail-closed `sta_join` path this lane hardens.
**Status:** static re-derivation + hardened patch + host tests + full
compile-verify **complete**; the bench ladder is **designed, not fired** (the
Lead fires; this lane never touches the device).

**No vendor binary/firmware/calibration byte is committed.** The pinned local
artifacts used for re-derivation are listed in sec. 2 and are never copied
into the repo; the tools print only derived offsets/ids.

**Evidence markers:** [V] re-verified by this lane from the named local
artifact by the committed tool; [V*] device-measured by an earlier attended
lane (cited doc); [I] inference; [U] unknown.

---

## 1. Headline

1. **The 108-byte WAL 0x123 body carries four addresses the FIRMWARE
   dereferences.** The vendor `wal_cfg80211_connect` stores the raw
   `cfg80211_connect_params` pointers into the body (no translation call,
   re-derived store-by-store in sec. 3). That works on the vendor kernel only
   because it runs **VMSPLIT_2G**: there `kmalloc VA == physical == the
   device-visible host-DRAM address`. **Our luofu 6.18 kernel is
   VMSPLIT_3G** (`PAGE_OFFSET=0xC0000000`), so a kernel VA is **not** a
   device address - passing one would hand the firmware a wild pointer. The
   hardened path therefore passes `dma_alloc_coherent` **device addresses**
   inside the bench-proven `0x80000000..0x8fffffff` window (the same window
   the proven SR ring nodes use) and refuses anything else.
2. **All four address fields are validated before a frame exists**:
   non-zero (no NULL sentinel - the firmware contract for a zero field is
   [U]), 4-byte aligned, inside the host-DMA window, inside the caller's
   coherent buffer, fixed buffer layout, `ssid_len`/`ie_len`/`key_len`
   fit their regions, no 32-bit wrap. Re-parsed from the finished body as a
   post-build self-check.
3. **The fire path is fail-closed and single-shot:** `sta_join=0` default =
   `-EOPNOTSUPP`; `sta_join=1` = dry run (builds + validates the real frame
   including the DMA-address math, copies **no key material**, posts
   nothing); `sta_join=2` = **one post per module load**, requires
   `allow_frames>0`, no retry on any failure, and **stops on the firmware
   mask/staging health witness** (firmware CPU `0x400008` exception bitmap /
   CPU `0x40000c` staged vector frame, already read by the HCC core's
   exception watch).
4. **The D2H assoc completion is decoded [V]:** the vendor registers the WAL
   chip-group-2 handler table with `hcc_msg_register_tab_chip(2, table,
   0x13)`; `id 3 = wal_asoc_comp_proc_sta` (feeds
   `cfg80211_connect_done`) and `id 4 = wal_disasoc_comp_proc_sta` (feeds
   `cfg80211_disconnected`). Payload field map: status at +0x02, bssid at
   +0x04, req_ie_len/resp_ie_len at +0x0c/+0x10, **firmware-supplied
   req_ie/resp_ie host-DRAM addresses at +0x14/+0x18**. Those addresses are
   **untrusted input**: this lane decodes them **log-only**, validates them
   against the host-DMA window, and never dereferences them.
5. **No key logging:** a static audit (23 checks) proves no
   `print_hex_dump`/`%*ph`, no `sme->key` pointer in any log, no
   frame/body dump, dry run gated by `with_key`, and all fail-closed gate
   evidence present.
6. **Compile-verify:** applies cleanly on the FINAL ALL-PATCH tree
   (opensource `008aca5` all-patch integration), zero-warning build on
   6.18.60, debug-stripped artifact **314,244 B sha256
   `e4e61184...`** (unstripped 1,156,840 B), **47 module parameters**
   (unchanged), `depends=cfg80211`, `vermagic 6.18.60 SMP mod_unload ARMv7
   p2v8`. Host selftests **111 checks PASS**, static re-derivation **86
   checks PASS**, safety audit **23 checks PASS**.

---

## 2. Inputs, conventions, method

| item | value |
| --- | --- |
| primary vendor module | 2.5.24 `hi5622v100_wifi.ko`, 3,564,728 B sha256 `de78ec07...` (local only, never committed) |
| firmware image | `hi_wifi/FIRMWARE.bin`, 928,920 B sha256 `7fc87e20...` (pinned by the scan-d2h lane; not needed by this lane's claims) |
| kernel target | luofu 6.18.60 (VMSPLIT_3G, `PAGE_OFFSET=0xC0000000`, `CONFIG_ARM_PATCH_PHYS_VIRT=y`) |
| DTS dma-ranges (target skeleton) | `<0x02000000 0x0 0x80000000 0x80000000 0x0 0x10000000>` - PCI 0x80000000 maps to CPU 0x80000000 **identity** |
| host-DMA window | `HI5622_HOST_DMA_LO/HI = 0x80000000/0x8fffffff` (same window the bench-proven ETE/SR path uses) |
| reproduction | `tools/fw_sta_join_re.py <local vendor .ko>` (86 asserted facts), `tools/selftest.sh`, `tools/build-sta-join-hardening.sh` |

Conventions: vendor `.ko` offsets are **link-time `.text` section offsets**
(the ARM module is ET_REL); firmware CPU addresses are not used by this
lane's claims. All analysis is static/read-only; no register was touched and
no frame was sent.

---

## 3. The 108-byte body pointer fields ([U] deref risk) - re-derived

### 3.1 The vendor builder, store by store [V]

`wal_cfg80211_connect` (2.5.24 `.text+0x100590`, size 0x2b4) memsets a
108-byte local body at `sp+0x18` (`mov r3,#0x6c` + `memset_s` at
`.text+0x1005ec/0x1005f8`) and stores, raw from
`struct cfg80211_connect_params` (5.10 layout):

| body off | store (link-time `.text`) | source |
| ---: | --- | --- |
| +0x01 | `strb r3,[sp,#0x19]` @0x100618 | `params+0x14` ssid_len |
| +0x02 | `strb r3,[sp,#0x1a]` @0x100644 | `params+0x28` mfp |
| +0x04 | `str r2,[sp,#0x1c]` @0x100680 | `params+0x1c` IE **pointer** (raw) |
| +0x08 | `str r3,[sp,#0x20]` @0x100600 | `params+0x10` SSID **pointer** (raw) |
| +0x0c | `str r3,[sp,#0x24]` @0x10061c | `params+0x08` BSSID **pointer** (raw) |
| +0x10 | `strb r3,[sp,#0x28]` @0x100638 | `params+0x24` privacy |
| +0x11 | `strb r3,[sp,#0x29]` @0x10062c | `params+0x18` auth_type |
| +0x12 | `strb r7,[sp,#0x2a]` @0x1006d4 | `params+0x78` key_len |
| +0x13 | `strb r3,[sp,#0x2b]` @0x1006dc | `params+0x79` key_idx |
| +0x14 | `str r2,[sp,#0x2c]` @0x1006d8 | `params+0x74` key **pointer** (raw) |
| +0x68 | `str r3,[sp,#0x80]` @0x100684 | `params+0x20` ie_len |
| +0x18..+0x67 | - | zero from the memset [V] |

Then `.text+0x100688/0x100694`: `wal_force_scan_complete(dev, 1)` and
`wal_cfg80211_start_connect(dev, body)`; `wal_cfg80211_start_connect`
(`.text+0x128310`) passes **opcode `0x123`, body len `0x6c`, wait=1** to
`wal_cfg80211_start_req`, which prepends the `{u16 opcode, u16 len}` WAL
data block (sec. 5). There is **no translation helper between the parameter
loads and the pointer stores** - the four values are raw host kernel
pointers. **No channel/frequency and no HT bandwidth field** exists in the
body [V] (the channel/band context comes from the VAP/scan state; the
bandwidth field is in the separate g0 id-0x0f join message, inherited
report sec 3.4).

### 3.2 Why the vendor can pass raw pointers, and why we cannot

* Vendor kernels for this board run **VMSPLIT_2G**; with `PHYS_OFFSET =
  0x80000000` (DDR at 0x80000000) a lowmem kmalloc VA is numerically
  `0x80000000 + (phys - 0x80000000)`. The DTS dma-ranges are **identity**,
  so that VA is also the device-visible host-DRAM address. The vendor
  firmware simply dereferences it.
* Our project kernel is **VMSPLIT_3G**: kernel VA = `0xC0000000 + phys -
  0x80000000`. A raw `params->ssid` pointer (`0xC0...`) is **outside** the
  device window; passing it would be an untranslatable address to the
  firmware = wild dereference class. [V for the config/DTS; I for the exact
  firmware fault behaviour]
* The proven convention in our driver is the `dma_alloc_coherent` device
  address: the bench-validated SR node path hands the firmware
  `0x8xxxxxxx` addresses and the firmware consumes them (TX completed, RX
  arm passed). The join body uses the **same convention**.

### 3.3 Hardening rules implemented (`src/hi5622_sta_frames.c`)

`hi5622_sta_join_bufs_check(bufs, buf_va, buf_size, dma_lo, dma_hi)`:

1. all four addresses **non-zero** - a zero field's firmware semantics is
   [U], so it is never emitted as an "absent" sentinel (the buffer always has
   a valid IE region and key region; lengths carry the absence);
2. 4-byte alignment;
3. each address inside `[dma_lo, dma_hi]` = `0x80000000..0x8fffffff`;
4. whole buffer `buf_va..buf_va+buf_size` inside the window;
5. **fixed layout only**: bssid = buf+0, ssid = buf+8, ie = buf+40,
   key = buf+552 (buffer 584 B);
6. `ssid_len ∈ 1..32`, `ie_len ≤ 512`, `key_len ≤ 32`, `key_idx ≤ 3`,
   auth_type ∈ 0..8;
7. each pointee region `[va, va+len)` inside the buffer, computed in u64 so
   no 32-bit wrap can pass.

The finished body is then re-parsed by
`hi5622_sta_connect_body_check()` (reserved bytes zero, every length/pointer
equal to the validated bufs). `hi5622_sta_connect_body()` itself repeats the
bounds and the non-zero rule. The buffer is written before a `wmb()`, and
freed only after a posted disconnect or module teardown, with the whole
buffer **zeroized before free** (the key region must not survive in a freed
coherent page).

**Residual [U]:** whether the firmware (a) treats those fields exactly as
device-visible addresses and (b) dereferences the IE/key fields
unconditionally when the corresponding length is zero. The no-zero policy
and fixed-layout validity make (b) harmless in the worst case (the firmware
would read zeroed, bounded bytes). (a) is the bench experiment: ladder B2 is
a **single controlled post**; any mask/staging change is the answer, not a
regression to retry.

---

## 4. The fire path: gates and lifetime

### 4.1 Gate matrix (`src/hi5622_sta_logic.c: hi5622_sta_join_gate`)

| sta_join | allow_frames | already attempted | result |
| ---: | ---: | ---: | --- |
| 0 (default) | any | any | `-EOPNOTSUPP`, nothing built |
| >2 | any | any | `-EINVAL` |
| 1 (dry) | any (even 0) | any | build + validate + log, free buffer, return `-EOPNOTSUPP` |
| 2 (post) | 0 | any | `-EPERM` (inherited post path is fail-closed) |
| 2 (post) | >0 | 1 | `-EALREADY` - **one join per load, no retry** |
| 2 (post) | >0 | 0 | proceed to the health gate + post |

`join_attempted` is latched **before** the device write, so every failure
(post refused by the inherited gate, timeout, core refusal) is final for the
load. A second `.connect` never reaches the wire. Disconnect has its own
`disconnect_attempted` latch (one post per load) and is a no-op
`-ENOTCONN` when no join was posted. `hi5622_allow_frames` (the inherited
per-load provider-frame budget) is required non-zero for a post, and the
inherited `hi5622_frame_post()` still enforces the full gate set (boot
latched, released, phase ACTIVE, lane open, live `out[0]==0`, SR window
check, structural + firmware-table validation).

### 4.2 "Stop on mask/staging" - mapped to the real registers

The bench read pack's stop rules are:
page `CA 0x001bf000` golden `0x86807fef/7/2/0xabcd`, state
`CA 0x00118b60 = 1`, **mask `CA 0x00400008 = 0`, staging `CA 0x0040000c =
0`**. Those two are firmware CPU addresses already defined by the project
as `HCC_CPU_EXCP_MASK = 0x00040008` (exception-seen bitmap) and
`HCC_CPU_VECTOR_FRAME = 0x0004000c` (17-word staged vector context frame),
read read-only by the HCC core's exception watch (`hcc_core.c`).

`hi5622_sta_health()` refuses the join (dry or post) with `-EIO` while the
core has latched a **non-zero exception mask** or has **captured any vector
frame**. Failure is final: log "STOP, no retry; power-cycle if nonzero".
This is the driver-side implementation of "stop on mask/staging"; the ladder
additionally has the human read-pack check before/after each rung.

### 4.3 Dry run never handles secrets

`sta_join=1` calls the same buffer builder with `with_key=false`: bssid,
ssid and IEs (already public over the air) are copied into the coherent
buffer so the real DMA addresses are built and validated; the key region is
left **zero**, the body carries `key_len=0` and `key_va` pointing at that
zeroed 32-byte region, and the frame is **never posted**. The buffer is
zeroized and freed before returning. The dry-run log prints lengths and the
four device addresses only - **never key material** (audit S2/S4).

---

## 5. WAL frame layout - verified

Same envelope as the bench-proven AP-start frames (inherited
`h2d_frames.c/h2d_check_wal_cfg()`, allowlist entry `{0x0123,108}` added
by the sta-static lane):

| off | width | value for a posted join |
| ---: | ---: | --- |
| +0x00 | u8 | group nibble 2 (WAL/core carrier) |
| +0x01 | u8 | `0x01 | (dst<<4)` (alloc 1, dst 0) |
| +0x04 | u16 | total = 32 + 108 = **140** |
| +0x06 | u16 | HCC id 0 (bench-proven WAL carrier) |
| +0x0a | u16 | tag 0x5a5a |
| +0x0c | u32 | WAL cookie 0 |
| +0x10 | u32 | WAL message class 1 (`WAL_MSG_CFG`) |
| +0x14 | u16 | pad 0 |
| +0x16 | u16 | data_len = 4 + 108 = **112** |
| +0x18 | u32 | sequence (incremented per cfg event) |
| +0x1c | u16 | opcode **0x0123** (disconnect **0x00d0**) |
| +0x1e | u16 | body_len **108** (disconnect **16**) |
| +0x20 | 108 B | body (sec. 3) |

Disconnect frame = 32 + 16 = **48 B**; body `{bssid[6]@0, reason u16@6,
0x00..; [15]=0x01}` (vendor constant re-derived:
`strb r3,[r4,#0xf]` @`.text+0x128360`; `mov r2,#0x10` /
`mov r3,#0xd0` / call `wal_cfg80211_start_req` @`.text+0x1283c0..cc`).
Both frames are structurally re-checked by `hi5622_sta_frame_check()` and
then by the inherited WAL/HCC validators inside `hi5622_frame_post()`.

---

## 6. D2H assoc completion decode [V]

### 6.1 Dispatch: WAL chip-group 2

`wal_main_init` (`.text+0x123cac`) calls
`hcc_msg_register_tab_chip(2, &table, 0x13)`; the table at
`.data+0x1c74` has 19 entries of 12 B `{u32 id; void (*handler); u32 arg}`.
Decoded ids (derived): 0 config, 1 acs, 2 `wal_scan_comp_proc_sta`, **3
`wal_asoc_comp_proc_sta`**, **4 `wal_disasoc_comp_proc_sta`**, 5/6 AP
connect/disconnect, 7 mic, 8 acs rsp, 9 `wal_send_mgmt_to_host`, 12/13
cfg80211 event/mgmt-tx-status, 20 cac, 21 rssi, 30 external-auth, 31
packet-xmit, 32 power-down, 37 multiap, 38 hiwifi.
So **D2H WAL g2 id 3 = STA assoc completion**, **id 4 = disassociation
completion**. `hcc_msg_register_tab_chip(2,...)`'s "2" is the WAL chip
group nibble; the received HCC header is 12 bytes and the payload starts at
wire offset 12.

### 6.2 Handler field extraction

`wal_asoc_comp_proc_sta` (`.text+0x125b6c`) takes the received frame
pointer `r6 = *(msg+0x118)`, looks up the netdev from `hdr[1]>>4` /
`hdr[3]`, and builds a 28-byte local struct from (all [V], instruction
addresses in TABLES T8):

* `ldrh r1,[r6,#0xe]` → status (u16)
* `memcpy_s(..., 6, r6+0x10, 6)` → bssid
* `ldr r2,[r6,#0x20]` / `ldr r3,[r6,#0x18]` / `ldr ip,[r6,#0x1c]` /
  `ldr r0,[r6,#0x24]` → req_ie addr / req_ie_len / resp_ie_len / resp_ie
  addr (staged at `sp+0x1c/0x1c/0x24/0x18`)
* success path calls `wal_update_bss(...)`; then
  `oal_cfg80211_connect_result(dev, &bssid, req_ie, req_ie_len, resp_ie,
  resp_ie_len, status, gfp=0xa20)`, which maps exactly onto 5.10
  `struct cfg80211_connect_resp_params` (`status@+0, bssid@+4, bss=NULL@+8,
  req_ie@+0xc, req_ie_len@+0x10, resp_ie@+0x14, resp_ie_len@+0x18`) and
  calls `cfg80211_connect_done`.

**Payload map (payload = wire bytes after the 12-byte HCC header):**

| payload off | width | value | evidence |
| ---: | ---: | --- | --- |
| +0x00 | u16 | **not read by the handler** [U] | - |
| +0x02 | u16 | status (0 = success; passed to cfg80211) | `ldrh r1,[r6,#0xe]` |
| +0x04 | u8[6] | bssid | `memcpy_s(...,r6+0x10,6)` |
| +0x0a | u16 | **not read by the handler** [U] | - |
| +0x0c | u32 | req_ie length | `ldr r3,[r6,#0x18]` |
| +0x10 | u32 | resp_ie length | `ldr ip,[r6,#0x1c]` |
| +0x14 | u32 | **req_ie host-DRAM device address** | `ldr r2,[r6,#0x20]` |
| +0x18 | u32 | **resp_ie host-DRAM device address** | `ldr r0,[r6,#0x24]` |
| 0x1c.. | - | end of the 28-byte struct | min length |

`wal_disasoc_comp_proc_sta` (`.text+0x125e3c`): payload +0x00 u16 = reason
code (`ldrh r1,[r4,#0xc]`), ie=NULL/len=0, then
`oal_cfg80211_disconnected(...)` → `cfg80211_disconnected`.

### 6.3 The IE addresses are untrusted input

The two addresses are **firmware-supplied host-DRAM addresses**. A naive
driver that casts them to pointers performs a wild kernel read if the
firmware value is wrong or stale. On our VMSPLIT_3G kernel the translation
must be the identity dma-ranges rule: device address 0x8xxxxxxx = CPU
physical 0x8xxxxxxx, so a deref would need
`phys_to_virt(devva)` (or the equivalent __va) after the same window check
used for the write path - **never a raw cast**. This lane deliberately does
**not** dereference them: `hi5622_sta_assoc_comp_decode()` validates
`addr < dma_lo/hi`, alignment, length ≤ 1024 and no wrap, and the d2h.c hook
logs the completion (status/bssid/lengths/addresses) **without calling
`cfg80211_connect_done()`**. Wiring the completion is the RX/AP-mgmt lanes'
call (coordination message sent to `ap-mgmt`; no overlap: this lane is
log-only).

### 6.4 Integration point

`d2h.c rd_deliver()` (the working direct group-A reader) calls
`hi5622_sta_d2h_deliver(ctx, fr->hdr, payload, plen)` for
`group==2 && (id==3 || id==4)`. The helper is **silent at `sta_join=0`**
(default) and only logs when the STA path is enabled; it can never post
anything. This makes ladder B2's dmesg self-explanatory ("did the firmware
emit g2 id3?") without adding unproven completion behaviour.

---

## 7. Implementation delivered

`patches/0001-sta-join-hardening.patch` (8 files) applies on top of the
all-patch integration tree (final module source of opensource `008aca5`):

| file | change |
| --- | --- |
| `hi5622_sta_logic.h/.c` | join gate + health gate + D2H completion decoders + hardened connect check (portable C99) |
| `hi5622_sta_frames.h/.c` | non-zero pointer policy, fixed-buffer layout, `join_bufs_check` (window/alignment/region/wrap), body re-parse check |
| `hi5622_sta.c` | dry-run/post rewrite: one-shot latch, health stop, allow_frames gate, no-retry semantics, buffer lifecycle + zeroize, log-only D2H delivery |
| `hi5622_sta.h` | new prototypes |
| `hi5622_cfg80211.h` | STA ctx one-shot latches + counters |
| `d2h.c` | 2-line log-only completion hook (no cfg80211 call) |

No module parameter is added or changed: the final module still exposes the
same **47** parameters (the two this path uses - `sta_join`,
`allow_frames` - already existed). In particular `sta_join` is now
read-only (`0444`): the mode cannot be flipped live on a boot that already
fired an attempt.

### 7.1 Verification results (host-only)

| gate | command | result |
| --- | --- | --- |
| patch applies cleanly + compiles with zero warnings | `tools/build-sta-join-hardening.sh` | PASS (evidence:`evidence/build-verify.txt`, `build.log`) |
| artifact | debug-stripped 314,244 B sha256 `e4e61184...`; unstripped 1,156,840 B; strip does not change `.text` | PASS |
| params/feature strings | 47 params, `sta_join`/`allow_frames` present, feature strings present | PASS |
| host unit tests | `tools/sta_join_selftest.c` | **111 checks PASS** |
| static re-derivation | `tools/fw_sta_join_re.py` | **86 checks PASS** |
| safety audit | `tools/audit_no_key_log.py` | **23 checks PASS** |

The artifact is **local evidence only** and is never committed (the build
script writes it under `/tmp/sta-join-hardening-build/`).

---

## 8. Corrections / additions to the inherited record

1. **The four body addresses are raw cfg80211 pointers, not a translated
   form.** The sta-static report noted "the equivalent on our kernel is a
   `dma_alloc_coherent` address" - correct, and this lane makes it a
   **verified requirement**: on the vendor build the raw pointer works only
   because VMSPLIT_2G makes VA == device address; on our VMSPLIT_3G kernel a
   raw pointer is out-of-window, so the address-kind check is part of the
   fail-closed gate. (§3.2)
2. **Zero is not a legal "absent" field.** The previous builder emitted
   `key_va=0`/`ie_va=0` when absent; the firmware contract for a zero
   address is unknown, so the hardened path always emits a valid zeroed
   region and carries absence in the length. (§3.3)
3. **One join per load is now enforced by the driver**, not only by the
   bench procedure (previous lane allowed repeated connects and a second
   post after a failure). The latch is set before the write, so no failure
   can be retried in the same module load. (§4.1)
4. **The "mask"/"staging" bench stop rules have driver-visible addresses**:
   firmware CPU 0x400008 / 0x40000c, already read by the HCC exception
   watch - so "stop on mask/staging" is implemented, not just documented.
   (§4.2)
5. **The D2H assoc completion is no longer undecoded.** The sta-static
   report listed "D2H management/assoc event decode" as open; this lane
   decodes the actual WAL g2 id-3 family (dispatch table + payload map +
   cfg80211 mapping) and flags the two firmware-supplied IE addresses as
   untrusted input. The full `cfg80211_connect_done` wiring is still left
   to the RX/AP-mgmt lanes. (§6)
6. **`wal_asoc_comp_proc_sta`/`wal_disasoc_comp_proc_sta` are WAL
   chip-group-2 ids 3/4** (the inherited hcc-re report lists the table; this
   lane pins the payload offsets and the wrapper mapping). (§6)

---

## 9. Open items ([U] / other lanes)

* **Firmware dereference contract of the 108-byte body addresses** - the
  one [U] the bench experiment B2 answers. One controlled post; any
  mask/staging change = answer = power-cycle, no retry.
* Payload bytes +0x00 and +0x0a of the assoc-completion event are not read
  by the vendor host handler [U]; they are exposed in the decoder struct for
  a future capture, not interpreted.
* The producer/translation of the two completion IE buffers (firmware
  allocates them where? which cache attributes?) [U]; until that is proven
  they stay log-only.
* `cfg80211_connect_done`/`cfg80211_disconnected` wiring after a validated
  decode = RX / AP-mgmt lanes.
* WPA2/WPA3 association needs the 4-way/key path and MAC RX (wpa-static /
  rx-static lanes); the ladder uses an **open** external AP first.
* 5 GHz and HT40 *use* remain the band5g / HT lanes' scope; this patch does
  not change their parameters or tables.

---

## 10. Reproduction

```sh
# host unit tests + static re-derivation + safety audit (no device)
./tools/selftest.sh

# full compile verification against the ALL-PATCH module tree
# (needs the 6.18.60 project tree + its staging dir)
./tools/build-sta-join-hardening.sh            # uses /tmp/wifi-allpatch-build/integ
INTEG_TREE=/path/to/integ ./tools/build-sta-join-hardening.sh
```

Bench ladder: [TEST-LADDER.md](TEST-LADDER.md) (B0-B4 + topology; **not
fired by this lane**). Tables: [TABLES.md](TABLES.md).
