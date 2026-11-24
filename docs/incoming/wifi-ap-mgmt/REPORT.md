# wifi-ap-mgmt - WAL mgmt-tx family + D2H mgmt RX decode (AP client assoc/auth)

**Lane:** wifi-ap-mgmt (shared task task-143), **host-only**.  **Written:**
2026-11-24.  **Status:** static reversal complete; fail-closed patch
host-built and compile-verified on the final all-patch tree (stability commit
`008aca5`); host tests and static-RE probes pass.  **NOT FIRED** - no bench
contact by this lane; the attended ladder is [LADDER-MGMT.md](LADDER-MGMT.md).

Evidence markers: **[V]** verified in a local artifact (vendor host module
2.4.15 sha256 `22fa789a...` / 2.5.24 sha256 `de78ec07...`, PD4 firmware
sha256 `cfe24bfb...`; CA = file offset + 0x40000); **[V*]** hardware-observed
in a prior attended session; **[I]** inference; **[U]** unknown.

---

## 0. TL;DR - the ten answers

1. **The vendor cfg80211 `.mgmt_tx` path is decoded end to end** [V].  The op
   (`wal_cfg80211_mgmt_tx` 2.4.15 0x1056D8 / 2.5.24 0x1058E0, 0x754 B,
   identical instruction stream +0x208) validates the request, builds a 12-byte
   descriptor and hands it to the unnamed helper `wal_mgmt_do_tx`
   (0x1042E4 / 0x1044EC), which emits **one WAL cfg-event TLV: cmd
   `0x01F2`, body 12** and waits for a completion.
2. **The descriptor is `{u32 channel; u8 cookie_idx; u8 pad; u16 frame_len;
   u32 frame_addr}`** [V] and the **frame bytes are not in the message**.
   `frame_addr` is a **device-visible host-DRAM address**; the vendor passes
   the cfg80211 buffer pointer because its kernel is VMSPLIT 2G/2G, which puts
   kernel VAs in the device window 0x80000000..0x8FFFFFFF.  This kernel is
   VMSPLIT 3G, so the clean-room driver copies the frame into its own
   `dma_alloc_coherent()` buffer (window-checked) and passes that DMA address -
   the same translation rule already documented in `hi5622_driver.h` for the
   ETE/HCC buffers [V]/[I].
3. **Completion is HCC group-2 id 13 `wal_cfg80211_mgmt_tx_status`** [V]:
   `data[12]` = status word -> `vif+0x3CC` (1 = success), `data[16]` = cookie
   slot echoed back; it sets `vif+0x3C8 = 1` and wakes the try loop, which then
   reports `cfg80211_mgmt_tx_status(ack=...)`.
4. **Probe requests are special-cased** [V]: if `(frame control & 0xfc) ==
   0x50`, the vendor completes the cookie immediately and posts **no**
   firmware frame (0x1059C8).  The clean-room op does not need this because
   hostapd's AP path uses auth/assoc/probe-response frames; it is recorded so
   no one expects a probe-request TESTMODE frame.
5. **The D2H management-frame RX event is fully decoded** [V]: HCC **group 1
   id 13**, frame at **payload+48** (after the 12-byte HCC header), total
   length = 60 + frame; the 48-byte RX info carries **channel at +7**,
   **u16 frame length at +17** (cross-checked against total-60 by the vendor)
   and **s8 RSSI at +39**.  Frequency uses the vendor formula
   (2.4 GHz `2407+5ch`, ch14 2484, 4.9 GHz `4000+5ch`, 5 GHz `5000+5ch`).
6. **Delivery is `cfg80211_rx_mgmt`** [V]: vendor chain
   `hmac_rx_process_mgmt_event` -> `hmac_rx_event_pre_process` (strip 60,
   copy 48 to `hcc_msg->cb`) -> AP FSM `hmac_ap_up_rx_mgmt` ->
   `hmac_rx_mgmt_send_to_host` -> `hmac_send_mgmt_to_host` (in-kernel
   group-2 id-9 hand-off) -> `wal_send_mgmt_to_host` ->
   `cfg80211_rx_mgmt_khz(wdev, freq*1000, sig=0, frame, len, gfp)` ->
   in 6.18 that is `cfg80211_rx_mgmt_ext`.
7. **EAPOL is not on this path** [V]/[I]: EAPOL (ethertype 0x888E) is an
   802.11 **data** frame, delivered by the vendor as a normal data RX frame
   (g0 id2/3 `hmac_rx_process_data_event` family) to the AP netdev, where
   hostapd's packet socket receives it.  The clean-room driver's RX data path
   currently arms/drains the MAC ring but does **not** deliver frames to the
   netdev, so a 4-way handshake needs that follow-up work as well as the
   frame TX/RX from this lane.
8. **Implemented fail-closed patch** (14 files, all original C): new portable
   logic `hi5622_mgmt_logic.c/.h` + kernel glue `hi5622_mgmt.c/.h`; a new
   provider capability `HI5622_FRAME_CAP_MGMT_TX` (bit 15) with the
   WAL-envelope builder in the vap-scan provider; `.mgmt_tx` routed to
   `hi5622_mgmt_op_tx()`; D2H hook in `hi5622_hcc_deliver()`; the WAL
   cfg-event command allowlist in `h2d_frames.c` extended with {0x01F2, 12};
   module params **`mgmt_tx=0` / `mgmt_rx=0` fail-closed defaults**; the
   cfg80211 symvers contract grows 10 -> **12 lines**
   (`cfg80211_rx_mgmt_ext`, `cfg80211_mgmt_tx_status_ext`).
9. **Host verification:** strict C99 selftest **60/60 PASS** (wire KATs,
   channel map, refusals, fail-closed defaults); static-RE probes **69/69
   PASS** against both vendor modules and the PD4 firmware; module
   compile-verified on the all-patch tree: **stripped 313,796 B sha256
   `a36ecf5495c72047943a40a9c49d5af9aa2f709a2280655eea1dfab4bedbcd7c`**,
   unstripped 1,173,568 B sha256 `36140410...`, **0 warnings**, **49
   params**, vermagic 6.18.60, `--strip-debug` leaves `.text` identical,
   12-line cfg80211 contract, both new imports present.  The .ko is local
   evidence only, never committed.
10. **Honest remaining blockers** (also reported to the Lead): (a) whether
    the PD4 firmware's WAL group-2 dispatch accepts cmd 0x01F2 and DMA-reads
    the frame is **[U]** - firmware static table A has no 0x01F2 row, and the
    firmware WAL dispatch table was not located statically; ladder M2 decides
    it; (b) host-SME association additionally needs
    `.add_station/.del_station` (hostapd `handle_assoc` -> NEW_STATION
    before the assoc response) and a firmware per-STA user context; (c) the
    WPA 4-way needs EAPOL data-frame delivery to the netdev; (d) the vendor's
    own AP SME lives in the hmac layer (`hmac_ap_up_rx_mgmt` builds
    auth/assoc responses itself and calls `cfg80211_new_sta`) - the fallback
    full-MAC design if the WAL TX carrier proves inert.

---

## 1. Scope, artifacts and method

Host-only static analysis plus original implementation, no device access.
Local artifacts read in place (never copied into the repository):

| artifact | size | sha256 | use |
| --- | ---: | --- | --- |
| vendor host module 2.4.15 | 3,581,748 B | `22fa789a3d43...c7d2` | full symbol table + relocation-aware disassembly |
| vendor host module 2.5.24 | 3,564,728 B | `de78ec07e46e...9521` | independent second version (identical code +0x208) |
| PD4 firmware | 928,920 B | `cfe24bfb1948...3037b1` | static config-key table A cross-check |

Method: pyelftools/capstone + llvm-objdump on the relocatable modules,
Thumb disassembly of the firmware at CPU addresses (file offset + 0x40000),
and direct instruction-word probes in [tools/mgmt_re.py](tools/mgmt_re.py)
(69 probes, all passing).  Every derived constant is in
[TABLES.md](TABLES.md); the tool re-derives the numeric claims from the local
binaries.

The project's earlier lanes had already decoded the WAL cfg-event envelope
(AP start: `wal_send_cfg_event`, cmd 0x0005/0x0127/0x0064/0x0065/0x0124;
WPA keys 0x0078/0x0079/0x007A) and the g0 id-1 config-key carrier.  This lane
reuses that envelope and adds the missing mgmt-tx command and the D2H RX
event.

## 2. Vendor host: the cfg80211 `.mgmt_tx` path [V]

Table T1/T2 in [TABLES.md](TABLES.md).  Summary:

    cfg80211 op #61 wal_cfg80211_mgmt_tx(wiphy, wdev, params, cookie)
      -> pre_proc (vif/user validation)
      -> mac_res VAP lookup, cookie counter at mac_res+0x18/0x1C
      -> probe-request short circuit (cookie ack, no frame)   [V]
      -> channel from params->chan (vendor requires non-NULL)
      -> descriptor {u32 channel, u8 cookie_idx, u8 pad, u16 len, u32 buf}
      -> wal_cfg80211_mgmt_tx_try(vif, desc, len, wdev, offchan, cookie,
                                  wait, buf)   (<= 8 attempts)
           -> wal_mgmt_do_tx(netdev, desc, offchan)
                -> vif+0x3C8 = 0; vif+0x3CC = 0x8B2D
                -> wal_send_cfg_event(netdev, 1, 16, tlv, 0, 0)
                -> wait on vif+0x3BC for vif+0x3C8 == 1 (50 ms slices)
                -> return vif+0x3CC
           -> success: copy cookie, clear slot, cfg80211_mgmt_tx_status(ack=1)
           -> failure: clear slot, cfg80211_mgmt_tx_status(ack=0)

The STA interface has a separate off-channel branch (0x104848 local function,
reached only when `wdev->iftype == STA` and
`wiphy->flags & 0x100000`); the AP path (iftype 2) always uses the WAL
carrier above.  That branch is outside this lane's scope (AP focus) and the
patch refuses non-AP interfaces explicitly.

### 2.1 The wire message [V]

    HCC header 12 B | cookie u32 | wal-id u32=1 | pad u16 | data_len u16 |
    seq u32 | cmd u16 | body_len u16 | body

* total wire length = **32 + body_len**; `data_len = 4 + body_len`
  (the "+4" is the cmd/len TLV header itself).
* the envelope is the vendor's WAL group-2 cfg-event family (HCC group 2,
  header id 0), the same family the AP-start/keys lanes already emit on the
  `ap_start_mode=1` carrier.
* mgmt-tx TLV: **cmd 0x01F2, body 12**, body = T3 descriptor.  Both shipped
  vendor modules emit the identical instruction sequence
  `movw rX,#0x1F2; strh; strh` exactly once in `.text` (probe H2).

### 2.2 The frame-address problem [V]/[I]

The descriptor's last word is the frame address the vendor passes to the
firmware.  Two static facts pin its meaning:

1. `wal_mgmt_do_tx` copies the **12-byte descriptor only** - the frame bytes
   are nowhere in the message (probe H2/H3) [V].
2. The value copied is `params->buf`, the cfg80211 frame pointer, and the
   project already knows (from the DP/HCC reverse-engineering) that the vendor
   kernel's host-DRAM window for the device is **0x80000000..0x8FFFFFFF** and
   that the vendor relies on a host-VA == device-address identity - a 2 GiB/
   2 GiB VMSPLIT accident (`hi5622_driver.h` states this rule and the
   project kernel is VMSPLIT 3G with kernel VAs at 0xCxxxxxxx) [V*].

Therefore the clean-room implementation **must** pass a device-visible DMA
address of its own coherent copy; passing the cfg80211 pointer would point the
firmware at the wrong memory.  The patch allocates a 588-byte coherent buffer,
copies the frame, checks the DMA address is inside
`HI5622_HOST_DMA_LO..HI` and refuses otherwise (fail-closed before any
device write).  Whether the PD4 firmware actually implements the DMA fetch for
this WAL command is [U] - table A has no 0x1F2 row (below) and the firmware
WAL dispatch table was not located statically.  Ladder M2 is built to
distinguish "firmware consumed but ignored" from "frame on air".

### 2.3 Firmware side cross-check [V]

The bench-proven **g0 id-1 carrier** dispatches through the PD4 static
config-key table A (CA 0x10C23C, 217 rows).  Probes F1:
keys 0x0124 -> 0x841B9 (channel), 0x0127 -> 0x84DA5 (AP config),
0x0005 -> 0x82879 (SSID), 0x0065 -> 0x46395 (VAP start) - and **0x01F2 is
absent**; the nearest neighbour 0x01F0 routes to 0x49C8D, a 6-byte-body
dispatcher (probe F2), not a frame TX worker.  So the mgmt-tx command cannot
be sent through the proven g0 carrier with a static key; it must ride the WAL
group-2 envelope.  This is the central risk of the lane and the reason the
ladder's M2 rung is a three-way decision, not a happy-path test.

### 2.4 Completion [V]

Device -> host HCC group-2 id 13, handler
`wal_cfg80211_mgmt_tx_status` (0x105E2C / 0x106034): `data[1]` high nibble =
dst/chip selector (must be 0), `data[3]` = mac-res VAP id, `data[12..15]` =
status word written to `vif+0x3CC`, `data[16]` = cookie slot.  It sets
`vif+0x3C8 = 1` and wakes `vif+0x3BC`.  The clean-room implementation does
not consume this event itself (no per-VAP cookie wait): `.mgmt_tx` reports
the cfg80211 status after the fail-closed frame-post gate confirms the
coalesced announce was consumed, which is the same "the firmware took the
message" fact the vendor waits for.

## 3. D2H management-frame RX [V]

Table T5 in [TABLES.md](TABLES.md).  Wire layout:

    HCC header 12 B | RX info 48 B | 802.11 frame (frame_len) B
    total = 60 + frame_len
    RX info: +7 channel u8, +17 u16 frame_len, +39 s8 RSSI

Vendor chain and the two independent cross-checks:

* `hmac_rx_event_pre_process` (0x49C28) strips 60 bytes when total > 59 and
  copies 48 bytes from `data+12` into `hcc_msg->cb`; it compares the u16 at
  `data+29` (= cb+17 = frame_len) with total-60 (probes H4).
* `hmac_rx_mgmt_send_to_host` (0x4277C) derives the frequency from
  `msg+31` (= cb+7 = channel) and the frame length from `msg+41` (= cb+17),
  then tail-calls `hmac_send_mgmt_to_host(vif, msg, len, freq)` (probe H5).
* `hmac_rx_mgmt_frame_notify_func` (0x48FB8) takes the RSSI from `msg+63`
  (= cb+39) and computes `clamp(100 + rssi, 0, 100)` for the frame-notify
  callback; the cfg80211 delivery path passes sig_dbm = 0 (unknown) - the
  clean-room decoder uses the raw s8 only when it is in [-120, 0] dBm (probe
  H6).
* `oal_cfg80211_rx_mgmt` (0x80C) calls
  `cfg80211_rx_mgmt_khz(wdev, freq*1000, 0, frame, len, 0xa20)` (probe H7).

Clean-room implementation: `hi5622_mgmt_deliver()` is called from
`hi5622_hcc_deliver()` for every frame on both receive paths (direct DR
reader and ETE engine path), filters group 1 id 13, and decodes with the
portable logic.  With `mgmt_rx=0` (compiled default) it only increments
counters.  With `mgmt_rx=1` it picks the AP `wireless_dev`, takes
`cfg->lock` with a **trylock** (never blocks the service thread, drops on
contention) and calls `cfg80211_rx_mgmt()`; it never writes a device
register and never retries.  Malformed frames (tag/length/type/channel/RSSI
checks) are refused and counted, never partially delivered.

## 4. EAPOL and the association chain - what this lane does and does not close

* **AP-side auth/assoc frames**: this lane delivers them up to hostapd and
  transmits hostapd's responses (host-SME model - the same model mac80211
  uses for nl80211 hostapd).  [I]
* **EAPOL**: 802.11 data frames (ethertype 0x888E); vendor delivers them via
  the data RX path to the netdev (`hmac_rx_process_data_event` 0x119E8
  family).  The clean-room RX data plane (wifi-rx-static lane) arms and drains
  the live MAC ring but does not yet hand frames to a netdev, so the 4-way
  handshake remains blocked on that follow-up.  [V]/[I]
* **Station ops**: hostapd's `handle_assoc()` calls `hostapd_sta_add()` ->
  `NL80211_CMD_NEW_STATION` **before** it sends the assoc response.
  cfg80211 rejects that without a `.add_station` driver op, so association
  cannot complete even with a perfect mgmt TX/RX path.  The vendor's own
  `.add_station` is an 8-byte stub because the vendor runs AP SME in the
  hmac layer (`hmac_ap_up_rx_mgmt` builds `hmac_mgmt_encap_auth_rsp` /
  `hmac_mgmt_encap_asoc_rsp_ap` itself and notifies hostapd with
  `cfg80211_new_sta`).  This is the next task, flagged to the Lead; the two
  candidate designs are in [TABLES.md](TABLES.md) T10.  [V]/[I]
* **WPA3/PMF**: the wpa3-sae lane (task-144) owns the AKM/cipher/IGTK side;
  this lane's mgmt path does not touch keys and composes additively (see
  section 8). [V]

## 5. Clean-room design and fail-closed gates

`.mgmt_tx` decision order (`hi5622_mgmt_op_tx`):

1. parameter sanity (wiphy/wdev/params/cookie/buf/len) -> -EINVAL;
2. `mgmt_tx=0` -> -EOPNOTSUPP **before any allocation or frame build**
   (compiled default; hostapd's frame-registration gate is satisfied by the
   op's presence, so the bench-proven image behaviour is unchanged);
3. `mgmt_tx > 1` -> -EOPNOTSUPP; non-AP interface -> -EOPNOTSUPP;
4. vendor rule: `params->chan == NULL` -> -EINVAL (the vendor logs "vap
   pchan ptr is null" and refuses; the AP frame goes out on the AP channel);
5. 802.11 management-frame sanity (protocol 0, type management, 24..588 B);
6. coherent DMA buffer (allocated lazily, DMA window checked) + frame copy;
7. provider capability `MGMT_TX` + builder (WAL envelope, cmd 0x01F2);
8. `hi5622_frame_post()` - the one wire path: `allow_frames` budget
   (default 0), inter-frame gap, armed/released/ACTIVE-lane gates,
   h2d_frames validator (the WAL cfg-event allowlist now includes
   {0x01F2, 12}), coalesced-announce consumption poll, no retry;
9. cfg80211 TX status with the real post result.

The RX path is read-only decode + a cfg80211 notification.  Both params are
`0644`, visible in `modinfo -p` and status output.

## 6. What was added to the final all-patch tree

[TABLES.md](TABLES.md) T8 lists all 14 files.  Highlights:

* `hi5622_mgmt_logic.c/.h` - portable, device-free C99: WAL envelope builder,
  12-byte descriptor builder, D2H event decoder, channel->frequency formula,
  management-frame sanity; every bound is a refusal.
* `hi5622_mgmt.c/.h` - kernel glue described above.
* `hi5622_frames_api.h` - `HI5622_FRAME_CAP_MGMT_TX (1u << 15)`,
  `struct hi5622_mgmt_tx_params`, `build_mgmt_tx` provider callback.
* `hi5622_vapscan_provider.c` - the builder wired into the registered
  provider; capability advertised.
* `h2d_frames.h/.c` - `H2D_WAL_CMD_MGMT_TX 0x01F2` and the
  {cmd, body_len} allowlist entry - the frame-post validator stays
  fail-closed for every unknown WAL command.
* `hi5622_cfg80211_ops.c` - the fail-closed stub body replaced by
  `hi5622_mgmt_op_tx()` (the `mgmt_diag` dump_stack probe is kept).
* `hcc.c` - one delivery hook next to the scan decoder.
* `hi5622_cfg80211.c` - teardown of the optional DMA buffer.
* `Kbuild` + `cfg80211-provider.symvers` (10 -> 12 lines).

## 7. Verification

| check | result | evidence |
| --- | --- | --- |
| strict C99 host compile, `-Wall -Wextra -Werror -pedantic` | PASS | [evidence/host-selftest.txt](evidence/host-selftest.txt) |
| `mgmt_selftest` (wire KATs, channel map, refusals, defaults) | **60/60 PASS** | evidence/host-selftest.txt |
| static fail-closed gates (params, gates, cap bit, patch content) | PASS | evidence/host-selftest.txt |
| static-RE probes, vendor 2.4.15 + 2.5.24 + PD4 firmware | **69/69 PASS** | [evidence/mgmt-re.txt](evidence/mgmt-re.txt) |
| module build on the all-patch tree + patch, 6.18.60, 0 warnings | PASS | [evidence/module-build.txt](evidence/module-build.txt) |
| module identity (patched) | stripped **313,796 B** sha256 `a36ecf5495c72047943a40a9c49d5af9aa2f709a2280655eea1dfab4bedbcd7c`; unstripped 1,173,568 B sha256 `36140410ad22e947bb1bf1b924ed26e5ac691c2a83d3802c074317fa01e414b8`; vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; depends=cfg80211 | evidence/module-build.txt |
| params | **49** (47 + `mgmt_tx` + `mgmt_rx`); `modinfo -p` lists both | evidence/module-build.txt |
| strip identity | `.text` byte-identical after `--strip-debug` | evidence/module-build.txt |
| cfg80211 contract | 12 lines; `cfg80211_rx_mgmt_ext` + `cfg80211_mgmt_tx_status_ext` imported; no other new imports | evidence/module-build.txt |
| uniqueness | `HI5622_FRAME_CAP_MGMT_TX` = bit 15, no duplicate cap bit | evidence/module-build.txt |

The artifact is **host-built and NOT fired as this combination**; the .ko is
local evidence only and is never committed.

## 8. Interface with the other static-RE lanes

* **wpa3-sae (task-144)** owns `wiphy->mgmt_stypes` (its AP TX mask
  `0x3c2a` widens the response bits; auth bit 11 was already present) and
  the AKM/cipher/IGTK/default-mgmt-key side.  This lane does not touch those;
  on merge take the wpa3-sae version.  Overlaps are only
  `hi5622_cfg80211_ops.c` (their ops-table line vs this lane's function body)
  and `hi5622_cfg80211.c` (their capability block vs this lane's teardown
  hook); both hunks are disjoint.  Merge order is irrelevant for behaviour.
* **beacon-static (task-140)** decoded the vendor host-side AP SME
  (`hmac_ap_up_rx_mgmt` -> `hmac_mgmt_encap_auth_rsp` /
  `hmac_mgmt_encap_asoc_rsp_ap` -> mgmt TX).  This lane confirms and extends
  that: the `wal_cfg80211_mgmt_tx` message family is now decoded, and the
  vendor's real AP-side SME is the hmac encap path, not hostapd.
* **wpa-static (task-139) / sta-static (task-141)** own keys and STA join;
  the mgmt TX/RX code here composes with them (no key/STA file changed).

## 9. Limits / open items

| # | item | status |
| ---: | --- | --- |
| 1 | Does the PD4 firmware dispatch WAL cmd 0x01F2 and DMA-read the frame? | **[U]** - table A has no 0x1F2; firmware WAL dispatch table not located; ladder M2 decides |
| 2 | `frame_addr` device-visibility rule | [V*]/[I] - project rule + vendor VMSPLIT identity; patch checks the DMA window and refuses otherwise |
| 3 | RX info offsets +18..+38 (rate/timestamp/status) | [U] - not needed for mgmt delivery |
| 4 | exact TX rate/flags the firmware chooses for a WAL-carried mgmt frame | [U] - the message carries no rate; firmware decides |
| 5 | association | blocked on `.add_station/.del_station` (host-SME) or the full-MAC AP SME fallback - next lane |
| 6 | WPA 4-way | additionally blocked on EAPOL data-frame delivery to the netdev |
| 7 | STA-side offchannel mgmt TX branch (0x104848) | out of scope (AP lane); a separate decode if STA connect needs mgmt TX |
| 8 | The g2 id-9 in-kernel hand-off message implies the vendor WAL/hmac layers pass host pointers internally | [I] - our driver bypasses it and calls cfg80211 directly |

## 10. Publication boundary

Host-only.  No vendor firmware/object bytes, calibration data, per-unit
identity or secret are committed; only derived addresses/offsets/ids/field
maps and original C.  The vendor modules/firmware stay in the local gitignored
evidence tree.  The patch is research code for the bench; the vendor firmware
path remains unsupportable upstream.  The ladder is a design artifact, not a
fired result.  This lane never contacted the bench router.

## 11. Reproduction

    cd opensource/docs/incoming/wifi-ap-mgmt
    ./tools/build-host.sh                       # 60/60 + gates
    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/mgmt_re.py                        # 69/69 probes
    ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-module.sh
