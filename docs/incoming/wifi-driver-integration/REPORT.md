# Hi5622 host-driver integration skeleton - transport<->hdpp interface + hdpp/alg core (clean-room)

**Lane:** `wifi-driver-integration` (task-64). **Written:** 2026-11-22.
**Deliverable:** original C + a marked transport delta + this report under
`opensource/docs/incoming/wifi-driver-integration/`. **No device access; no
vendor source/object code; no vendor binary, firmware, INI or calibration
data; no OpenWrt image build.** Every claim carries the project's evidence
marker: [V] re-verified from a named local artifact, [V*] device-measured by
an earlier attended lane and recorded in a doc, [I] inference, [U] unknown.

**Builds on:** `wifi-transport-port/` (the working B1/B2 transport skeleton),
`phase37/THE-PROTOCOL-MAP.md` (12-byte HCC header, boot dialogue, D2H service
routine), `wifi-driver-re/REPORT.md` (hcc_msg_process group/id dispatch,
16-byte handler records, D2H/H2D register decode), `wifi-framework-port/`,
`wifi-datapath-slice/` (hdpp/alg inventories) and `phase42/phase43`
(ETE registers, device-side preconditions).  The `wifi-hcc-re` lane
(task-63) landed its post-B1 contract **during this lane** (submodule commit
`07b9201`, `opensource/docs/incoming/wifi-hcc-re/REPORT.md`); the driver
skeleton was reconciled to it, and the remaining post-B1 gaps are marked [U]
with the exact probe that closes them.

---

## 0. Verdict

1. **The transport<->driver interface is now defined and implemented.**
   `interface/hi5622_transport_iface.h` is the contract; the client/ops
   implementation lives in `interface/pciet_iface.c` and is compiled into the
   transport module by a **6-hunk marked delta**
   (`transport-delta/0001-pciet-integration-iface.patch`).  The transport
   exports two symbols; the driver binds through an ops table that covers
   HCC send, a D2H dispatch table, TX/RX ETE ring service and firmware
   ready/state callbacks (section 2).
2. **The hdpp/alg core skeleton is implemented and compilable.**  It contains
   the HCC message layer (12-byte header, group/id dispatch, 16-byte handler
   records), a TX queue to the SR0 ring + id-3 announce, the D2H dispatch
   table for mailbox ids 0..10, the two HCC group-4 boot completes, the ETE
   ring service wrapper, hdpp core/object-tree placeholders, an alg
   configuration skeleton (protocol disabled because its response id is [U]),
   a real netdev and a wiphy stub.  Three modules build clean against the
   project's 6.18.60 tree (section 5, `BUILD-NOTES.md`).
3. **The post-B1 contract is now reconciled against hcc-re (task-63).**  The
   driver's HCC layer models the three vendor register spaces (chip + two
   cores), the hdpp registration tables now carry the **pinned ids** (chip
   group 3: 0,4,5,7,8; core group 0: 0,1,2,3,4,7,8), the mailbox set is the
   vendor's {1,3,6,7}, and the "no H2D reply makes ready" rule is implemented.
   Reconciling it also found **transport-side changes B3 needs** (DR
   free-buffer index register, index phase bits, arming all seven channels) -
   documented, not silently patched: section 7 and
   `transport-delta/README.md`.
4. **Hardware attachment is staged, not executed.**  This lane never touched
   the device.  The transport lane's **stage B2 passed on 2026-11-22 18:35**
   (ETE ring programming + H2D smoke; see `mem-entries.md` 18:35 and the
   transport commit `9700250`), so the condition "next steps once B2 is
   bench-verified" is now satisfied - section 10 is the concrete B3 plan,
   including a writable one-frame bring-up trigger in the driver.

---

## 1. Layering and file map

```
  userspace: ip/link only (netdev skeleton)          cfg80211: STUB (config = n)
        |                                                        :
        v                                                        :
  hi5622_hdpp_drv.ko  ==========================================================
   hi5622_drv_main.c   module glue, chips, state machine, attended gates
   hi5622_netdev.c     netdev skeleton + wiphy stub
   hi5622_d2h.c        mailbox D2H ids 0..10 + HCC group-4 boot/ready table
   hi5622_hcc.c        12-byte HCC header, tx queue, (group,id) dispatch
   hi5622_ete.c        TX/RX ETE ring service wrapper + counters
   hi5622_hdpp_core.c  hdpp core: tables, object tree, channel setup, tx/rx
   hi5622_alg.c        alg cfg store + (disabled) config-sync skeleton
  ============================== ops table ===================================
  hi5622_pciet.ko  ============================================================
   pciet_main.c        PCIe probe, iATU windows, fw upload, boot words, D2H
   pciet_ete.c         SR0/DR0 ring allocation/init/post/drain
   pciet_iface.c       [delta] client registry, ops implementation, D2H
                       service routine + dispatch, DR payload fan-out
  ============================== PCIe RC (013 patch) ============================
  59e7:0005 endpoint  x2 (one per RC) -> BAR0 16 MiB six-region window, BAR2 iATU
```

| path | what |
| --- | --- |
| `interface/hi5622_transport_iface.h` | the contract (ops table + client callbacks + state bits + hooks) |
| `interface/pciet_iface.c` | transport-side implementation (compiled into `hi5622_pciet.ko` by the delta) |
| `transport-delta/0001-pciet-integration-iface.patch` | the 6 marked hunks against `wifi-transport-port/build/{pciet_main.c,Kbuild}`; `README.md` documents the baseline hashes and review notes |
| `driver/*` | the hdpp-style driver skeleton (7 objects, 3 headers) |
| `mock/hi5622_transport_mock.c` | simulated transport: single self-contained module, no hardware |
| `tools/build-modules.sh` | the normative reproduction script (3 modules + verification) |
| `BUILD-NOTES.md` | build results, hashes, build-line, 6.18 fixes, reproduction |

---

## 2. The transport<->driver interface (requirement 1)

### 2.1 Contract shape

The driver does not poke the transport's `struct hi5622_pci` and never
touches a BAR.  It registers a `struct hi5622_transport_client`
(`name, owner, priv, attach, detach, fw_state, rx`); on `attach()` it
receives an opaque `struct hi5622_transport *` plus a
`const struct hi5622_transport_ops *`.  The transport keeps PCI ownership,
BAR mappings, DMA rings and every device write.

```
hi5622_transport_register_client(&client)      EXPORT_SYMBOL_GPL
hi5622_transport_unregister_client(&client)    EXPORT_SYMBOL_GPL
```

Two exports only; that is deliberate (minimum new transport surface).  The
transport also exposes `hi5622_tr_state_str()` as a normal (not ksymtab)
helper for logging.

### 2.2 Ops table (all function pointers non-NULL after attach)

| op | semantic | implemented by |
| --- | --- | --- |
| `h2d_send(t, msg, len)` | post one complete HCC frame (12-byte header + payload): copy into next SR0 buffer, post the 8-byte node `{devva,(len<<16)|0x6d2b}`, commit producer, announce **mailbox id 3** | `pciet_ete_sr_send()` + `pciet_msg_send(ctx, 3)` (the transport's B2-proven "send" path) |
| `h2d_kick(t, mailbox_id)` | raw boot announce; **id 3 only** (H2D id 5 is a firmware BUG() trap, phase37 1.1 step 3 [V]); the iface refuses anything else | `pciet_msg_send()` |
| `d2h_register(t, id, fn, arg)` | install a handler for mailbox id 0..10; -EINVAL/-ENOTCONN/-EBUSY | iface handler table (chained after the transport's own boot handlers) |
| `d2h_unregister(t, id)` | drop a handler | iface |
| `ete_init(t)` | program SR0 (group A) + DR0 (group B) base/depth/index registers from the probe-time DMA rings; **device writes, bench-gated** (see `allow_device_writes`, section 9) | `pciet_ete_ring_init()` twice - byte-for-byte what the B2 "ringinit" sysfs step did |
| `ete_tx(t, data, len)` | post one SR0 node without the mailbox announce (bring-up granularity) | `pciet_ete_sr_send()` |
| `ete_rx_service(t)` | drain DR0 completed nodes, deliver each payload to the client `rx()`; returns count | `pciet_ete_dr_consume()` |
| `ete_depth/ete_buf_len` | ring geometry (B2 measured depth-1 = 0x1f, 2 KiB buffers) | `ctx->sr0` |
| `get_state(t)` | snapshot mask (probed/windows/fw/released/gate-arm/plat-ready/device-ready) | `iface_compute_state()` from transport flags + completions |
| `wait_ready(t, mask, ms)` | sleep until state mask complete (poll-driven D2H, section 7) | waitqueue woken by the iface dispatch |
| `d2h_count(t)` | serviced D2H bitmap events (diagnostics) | iface counter |

The client callbacks: `attach/detach` (process context, may sleep),
`fw_state(t, state, d2h_id, priv)` (atomic; called on every state transition
and once on attach with `0xff`), `rx(t, buf, len, priv)` (atomic; one
completed DR payload = one HCC frame, buffer valid only for the call).
The full header is documented inline in
`interface/hi5622_transport_iface.h`.

### 2.3 H2D path (the vendor's only steady-state H2D mechanism)

```
driver builder (hcc layer) -> 12-byte header + payload in an hcc_msg
  -> hi5622_hcc_tx() (queue) -> tx worker -> ops->h2d_send()
     -> transport: SR0 node {devva, (len<<16)|0x6d2b}; producer commit;
        pending mask 0x40039010; doorbell drain-poll 0x400392d4; doorbell write
```

phase37 2.1 [V]: every H2D message is one SR node + announce **id 3**; the
per-message discriminator is the HCC header, never the mailbox id.  B2
device-proved this exact path with the transport's sysfs `send` step
("H2D smoke: 64 B posted on SR0, doorbell mask 8 (id 3)", no abort).  What
B2 has **not** yet proved is a *well-formed* HCC header causing a device
reaction - that is the B3 step (section 10).

### 2.4 D2H path: the phase37 1.2 service routine + dispatch

When `driver_mode=1` (section 9) and a client is attached, the iface replaces
the transport's legacy boot-only scan for that scan cycle:

1. read the D2H bitmap (CA 0x40039014) **without writes**; idle cycles are
   read-only;
2. on a non-empty bitmap, follow the phase37 1.2 fixed order [V]:
   ack = 1 -> CA 0x40101438 (out[3]), read + clear the bitmap, re-arm = 1 ->
   CA 0x40101414 (out[4]);
3. dispatch the set bits **lowest id first**: first the transport's own boot
   handlers (snapshot taken at probe: ids 1/2/3 today), then the driver
   handler registered for that id;
4. refresh the state mask, wake `wait_ready()` waiters, call `fw_state()`.

With `driver_mode=0` or no client, the hook fast-returns and the transport
behaves exactly as before (legacy read/drain/dispatch; DR payloads logged).
This is the safety design: the ack/re-arm writes are new device writes, so
they are enabled by an explicit attended step, not by module load.

Mailbox-id coverage in the driver (`hi5622_d2h.c`): the full 11-entry table
is implemented; the vendor's registration set {`1` dev-ready stub, `3` ETE
transfer-done, `6`/`7` ETE-sending pump} is registered, plus `2` and `4`
as **observability ids [I]** (see section 7 for the bit-2 divergence); ids
`0,5,8,9,10` stay unregistered exactly as the vendor leaves them.

### 2.5 TX/RX ETE ring service

* `ops->ete_init` = the B2-verified channel programming (SR0 base/depth-1/
  prod/cons in group A, DR0 in group B; bases land in the 0x8xxxxxxx DMA
  window once the RC inbound window exists - B2: SR0 0x8360c000, DR0
  0x83634000, depth-1 0x1f [V*]).
* `hi5622_ete.c` wraps TX (only through `h2d_send`, so SR + announce always
  move together), RX (`ete_rx_service` -> payload -> `rx()` -> HCC demux) and
  keeps counters.  The transport still owns the node format and the
  devva->VA buffer table.
* Ring-full/back-pressure is **not** modelled by the transport post path
  (`pciet_ete_sr_send` does not check the consumer); the driver queues at
  most 32 messages per work run.  Burst/back-pressure semantics are [U].

### 2.6 Firmware-ready / state callbacks

Two independent paths, both implemented:

* **Transport-level**: `ops->get_state()/wait_ready()` read the transport's
  flags and its `plat_ready`/`host_ready` completions; `fw_state()` is pushed
  on change.
* **HCC-level (phase37-correct)**: the driver registers the HCC group-4 boot
  table (5 entries [V]) and its id 1 `device_plat_ready_msg_process`
  completes `chip->plat_ready`, id 2 `host_ready_msg_process` completes
  `chip->device_ready` and schedules the setup work.  This is the load-bearing
  boot completion per phase37 3.2; the mailbox ids carry no body and the
  vendor answers none of them.

### 2.7 Why exports alone were not enough -> the delta

The transport's D2H path only reads/drains the bitmap and dispatches its *own*
static boot handlers; it had no client registry, no ack/re-arm routine, and
its DR consumer logs payloads instead of forwarding them.  The delta adds a
new file that implements all of that (plus the 2 exports) and 6 tiny hooks in
the existing code.  Details, baseline hashes and review notes:
`transport-delta/README.md`.  The transport lane keeps ownership of its files:
the patch is delivered as a patch, and the two new files are copied next to
the transport sources at build time by `tools/build-modules.sh`.

---

## 3. Transport delta in one table (requirement 1, "add it, clearly marked")

| # | location | marked hunk |
| --- | --- | --- |
| 1 | `pciet_main.c` includes | include the interface header |
| 2 | `pciet_d2h_payload()` | `if (pciet_iface_deliver_rx(ctx, va, len)) return;` |
| 3 | `pciet_d2h_scan()` | `if (pciet_iface_d2h_client_scan(ctx)) return;` |
| 4 | `pciet_probe()` | `pciet_iface_transport_added(ctx)` |
| 5 | `pciet_remove()` | `pciet_iface_transport_removed(ctx)` (waits for callbacks) |
| 6 | `Kbuild` | `hi5622_pciet-y += pciet_iface.o` |

All hunks are tagged `[driver-port integration delta]` in the patched source.
The patched module is behaviourally identical with no client or
`driver_mode=0`.

---

## 4. hdpp/alg core skeleton (requirement 2)

### 4.1 HCC message layer (`hi5622_hcc.c/.h`)

* Implements the **12-byte header** of phase37 2.2 [V] (the 10-byte header in
  `hi5622_pciet.h` is the older wifi-driver-re decode; it is untouched in the
  transport and not used here):
  `+0 group | +1 low nibble alloc-state / high nibble resource-group |
  +2/+3 per-message fields | +4 u16 total len (payload+12) | +6 u16 id |
  +8 field C | +9 retry | +0xa u16 SR tag 0x5a5a | +0xc payload`.
  Length is enforced against the vendor alloc cap `0x674 + 12`.
* The dispatch model is the vendor's **three register spaces** (hcc-re 6.3
  [V]): space 0 = the chip map (plat group 4, hdpp group 3, hmac groups 1/2/3,
  wal group 2 all merge into it by id), spaces 1/2 = the two per-core maps
  (hdpp core group 0).  `hi5622_hcc_register_tab_chip(group, tab, count)`
  installs into chip space; `hi5622_hcc_register_tab_core(group, tab, count)`
  installs the same table into both core spaces (the vendor's
  `customise(1)+customise(2)`).  Registrations merge by id and the group count
  tracks highest-id+1, which is what makes hmac's 8 ids and hdpp's 5 ids share
  chip group 3.  12-byte registration records -> 16-byte dispatch records;
  layout locked with `BUILD_BUG_ON` (12/16/12 bytes).
* `hi5622_hcc_process()` = `hcc_msg_process` mirror: group = `data[0]&0xf`,
  rgroup = `data[1]>>4`, id = le16@+6, bounds-check against the group table,
  stats, call handler.  Unknown (group,id) is counted and logged (rate-limited).
* TX: `hi5622_hcc_msg_alloc()->set_hdr()->hi5622_hcc_tx()` queues onto a
  workqueue; the worker posts through the ETE service (one frame per SR node,
  announce id 3) and frees the message.  `hi5622_hcc_kick()` is the D2H
  id-6/7 pump wake.

### 4.2 D2H dispatch (`hi5622_d2h.c`)

* Mailbox table ids 0..10 (full table with contract names; vendor set
  registered, section 2.4).
* HCC group-4 boot table: id 0 device-log blob (bounded log), id 1 plat-ready
  (`complete` + state), id 2 DEVICE READY (`complete` + state + setup work),
  id 3 exception info, id 4 heartbeat.  These mirror the phase37 3.2 handlers
  `oam_rx_post_action_function / device_plat_ready_msg_process /
  host_ready_msg_process / exception_info_msg_process / heartbeat_msg_process`.
* `hi5622_d2h_rx_frame()` is the transport `rx()` entry: counts, then hands
  the frame to the hdpp RX path.

### 4.3 ETE ring service (`hi5622_ete.c`) - section 2.5.

### 4.4 hdpp core (`hi5622_hdpp_core.c/.h`)

* `hi5622_hdpp_init()` builds the minimal board/chip/band/device object
  placeholders (opaque structs; layouts [U]) and registers the two vendor
  tables with the **pinned ids** (hcc-re 5.5 [V], `hdpp_main_init` `0x12e0`,
  calls `0x135c`/`0x136c`): chip-space group 3 with ids **0,4,5,7,8**
  (contract handlers `hmac_voice_aggr_event`, `hmac_device_wow_data_report`,
  `hmac_rx_schedule_req`, `hdpp_stat_save_tx/rx_ppdu_record_process`) and the
  per-core group 0 table with ids **0,1,2,3,4,7,8** (`hmac_tx_complete_*`,
  `hmac_tx_event_process`, `hmac_rx_process_data_event`, `hmac_mac_exception_proc`,
  `hmac_ba_timeout_proc`).  Handler bodies are stubs (they name the contract
  function and log); the real bodies belong to the hmac/hdpp port slices.
* `hi5622_hdpp_channel_setup()` = ETE ring programming + hdpp-up, gated by
  the attended `allow_device_writes` module parameter.
* `hi5622_hdpp_tx_frame()` (from netdev xmit) copies the skb into an HCC
  control frame (group 0, hex placeholder id `0x20`, resource group 1) and
  queues it; the real hdpp TX MSDU ring format is [U] and this path is
  explicitly marked as a placeholder.
* `hi5622_hdpp_rx_frame()` is the HCC demux entry for D2H frames.
* Tree/accessors: `hi5622_hdpp_band_get()`, `hdpp_vap_get()`,
  `hi5622_hdpp_state_str()`.  The full 238-function hdpp library is *not*
  duplicated here: `wifi-datapath-slice/scaffolding/hdpp_skel.c` already
  stubs that ABI; this file is the integration core.

### 4.5 alg skeleton (`hi5622_alg.c`)

* Opaque descriptor store for the 414-entry vendor alg/cali table [V*]
  (`hi5622_alg_cfg_store`, `hi5622_alg_cfg_dump`).
* Config-sync request builder uses the phase37 2.3-quoted H2D id
  `hmac_config_alg_send_event` = HCC **group 0 id 2** [V] with a
  skeleton-defined payload `{seq, cfg_id, len, data...}` (real layout [U]),
  and a completion wait.
* **Disabled by default** (`alg_protocol=0`) because the D2H HCC id that
  carries `hmac_sync_dmac_alg_cfg_rsp_entry` is [U]; a request would
  otherwise burn a 1 s timeout.  `hi5622_alg_cfg_response()` is the hook the
  future real handler calls when the id is pinned.

### 4.6 netdev + wiphy stub (`hi5622_netdev.c`)

* Real `alloc_netdev`/`register_netdev` skeleton: `ndo_open` (gates on
  RUNNING), `ndo_stop`, `ndo_start_xmit` (feeds `hi5622_hdpp_tx_frame`),
  carrier/queue reflected from the state machine.  MAC address is a local
  random placeholder (real hmac/bdinfo path [U]; no per-unit value ever
  committed).
* **wiphy is a stub by necessity**: the project's 6.18.60 `.config` has
  `# CONFIG_CFG80211 is not set`, so no cfg80211 object can exist.  The stub
  records the planned surface (2 bands, 39 cfg80211 ops from
  wifi-driver-re 2.2 [V]).  Full cfg80211 integration is explicitly out of
  scope for this task; the follow-up is the `wal_cfg80211` surface from the
  GPL-2.0 hi3881 framework tree (wifi-framework-port M4 plan).

### 4.7 Module lifecycle and state machine (`hi5622_drv_main.c`)

```
module_init -> hi5622_transport_register_client()
 transport probe / already probed -> client.attach()
   chip alloc, ordered wq, hcc init, d2h tables, hdpp/alg init, wiphy stub,
   netdev register, chip published (RCU list), state ATTACHED|TABLES
   adopt transport ready state if already complete
 HCC group-4 id 1  -> PLAT_READY (completion + state)
 HCC group-4 id 2  -> DEVICE_READY -> setup_work
   setup_work: allow_device_writes? -> hdpp_channel_setup -> CHANNELS -> RUNNING
 transport remove / module exit -> client.detach(): state DETACHING, cancel
   works, netdev unregister, d2h/hcc/hdpp/ete teardown, synchronize_rcu, free
```

State bits: `ATTACHED, TABLES, PLAT_READY, DEVICE_READY, CHANNELS, RUNNING,
DETACHING, FAILED`.  State observers run from a work item (they may take
rtnl), while transitions are recorded atomically from D2H context.

### 4.8 Mock transport (`mock/hi5622_transport_mock.c`)

Self-contained simulation of the whole client/ops interface: reports the full
ready mask, injects the phase37 group-4 id-1/id-2 boot frames through the real
`rx()` path, echoes each H2D frame with a heartbeat, simulates the ETE rings
and offers a `mock_ete_denied` knob.  Built together with the driver sources
into one module (`hi5622_hdpp_mock.ko`, **no hardware at all**):

```sh
insmod hi5622_hdpp_mock.ko allow_device_writes=1
dmesg | grep -E 'mock|hcc|hdpp|d2h|setup|state'
rmmod hi5622_hdpp_mock
```

This is the executable proof of the attach -> boot-complete -> channel-setup
-> RUNNING -> H2D/HCC -> D2H dispatch logic on any 6.18 test kernel.  It is
**not** a hardware validation and must never be used on the bench in place of
the real split build.

---

## 5. Build (requirement 3)

`tools/build-modules.sh` builds **three** modules against the project's built
6.18.60 luofu tree with the OpenWrt ARM toolchain (GCC 14.4.0), module builds
only - no `make world`:

| module | role | result |
| --- | --- | --- |
| `hi5622_pciet.ko` | transport + integration delta | builds clean; exports the 2 interface symbols |
| `hi5622_hdpp_drv.ko` | the driver skeleton, split build | builds clean; exactly 2 unresolved `hi5622_*` (the transport exports, linked via `KBUILD_EXTRA_SYMBOLS`) |
| `hi5622_hdpp_mock.ko` | driver + simulated transport | builds clean; fully self-contained |

All three: `vermagic=6.18.60 SMP mod_unload ARMv7 p2v8`, licence GPL, zero cc
warnings under the tree's `-Werror`; the only modpost note is the known
`CONFIG_MODULE_STRIPPED=y` `MODULE_DESCRIPTION` tree artifact.  Sizes,
sha256s and the exact commands are in `BUILD-NOTES.md`.  The verified .ko
artifacts live outside the repo in `~/wifi-driver-port-work/` (text only is
committed, per project rule).

`CONFIG_CFG80211 is not set` on this tree - the wiphy is a stub, see 4.6.

---

## 6. Implemented vs stubbed

| area | state | notes |
| --- | --- | --- |
| transport<->driver client/ops contract | **implemented** | 2 exports, 13 ops, 4 client callbacks, RCU + cb_active lifetime, `driver_mode` gate |
| phase37 1.2 D2H service routine | **implemented, not HW-run** | ack/clear/re-arm + lowest-first dispatch; gated by `driver_mode=1` |
| D2H dispatch table ids 0..10 | **implemented** | vendor set {1,3,6,7} [V] + observability {2,4}; full 11-entry table documented |
| HCC group-4 boot/ready completes | **implemented** | phase37-verified semantics; completes + state machine |
| HCC 12-byte header + space/group/id dispatch | **implemented** | 3 register spaces x 5 groups; 16-byte dispatch / 12-byte registration records; merge-by-id |
| HCC TX queue -> SR0 + id 3 | **implemented, transport path B2-proven** | well-formed-frame reaction is B3 [U] |
| ETE ring TX/RX service | **implemented** | wrapper; ring-full/back-pressure [U] |
| hdpp core (tables, object tree, channel setup) | **skeleton** | registration sites/sizes/ids [V] (hcc-re 5.5); handler bodies and object layouts still stubs |
| hdpp TX/RX data path | **stub** | netdev TX currently rides the HCC control path with a placeholder id; the real MSDU ring is [U] |
| alg cfg | **skeleton, disabled** | store/dump work; protocol response id [U] |
| netdev | **implemented skeleton** | register/open/stop/xmit; no real MAC/data path |
| wiphy / cfg80211 | **stub** | CONFIG_CFG80211=n on this tree; 39-op plan recorded |
| firmware boot handshake | **adopted from transport** | driver observes/adopts transport ready state; HCC group-4 is the authoritative completion |
| mock transport + module | **implemented** | compile + logic smoke test, no hardware |
| hardware bring-up (B3) | **planned, instrument ready** | section 10 |

---

## 7. Contract reconciliation with hcc-re, and the transport changes B3 needs

### 7.1 Items the hcc-re contract (task-63, submodule commit `07b9201`) resolved

| # | earlier assumption / open point | reconciled fact |
| --- | --- | --- |
| 1 | B1 saw `out[1]=0x4` and the transport logged "DEVICE READY (id 2)"; is bit 2 the ready handshake? | **No.** It is the firmware's platform-up announce; the vendor PCIe layer has **no handler for id 2** and drops it. The real handshake is two **HCC payloads** on the DR ring: chip group 4 id 1 (`device_plat_ready`) then id 2 (`host_ready`, payload <= 0x63 B). **No H2D reply** to either. The driver implements both paths (mailbox observability + HCC group-4 completion). |
| 2 | D2H mailbox registration set {1,3,6,7} vs transport {1,2,3} | confirmed **{1,3,6,7}** only; ids 0,2,4,5,8,9,10 NULL/dropped. The iface snapshots the transport boot handlers and the driver registers the vendor set (+ observability 2/4 for B3). |
| 3 | HCC header 12 bytes (phase37) vs 10 bytes (transport header) | 12 bytes confirmed (alloc-state/group nibble, u16 len at +4, id at +6, retry, 0x5a5a tag, payload +0xc). The driver implements the 12-byte form; the transport's 10-byte struct is now only documentation debt. |
| 4 | HCC dispatch grouping | **three register spaces**: type 0 = chip map (groups 1/2/3/4 merge by id), types 1/2 = the two per-core maps (hdpp core group 0). The skeleton was refactored from a flat 5-group table to `space[3][group[5]][id]`. |
| 5 | hdpp table ids were [U] | **pinned**: chip group 3 = ids 0,4,5,7,8; core group 0 = ids 0,1,2,3,4,7,8 (harmless: earlier placeholders 0..4/0..6 are replaced). Handler bodies still belong to the hmac/hdpp port. |
| 6 | is an H2D handshake required after DEVICE READY? | **No.** The vendor sends nothing between ready and normal operation; the first H2D traffic is whatever the driver configures (generic config events are chip group 0 id 1 / id 2). The B3 candidate matrix therefore starts from those. |
| 7 | ETE channel enable order | the channel `+0x00` enable is set **by firmware**, never by the host (no host caller of `pcie_ete_chn_enable`). The host programs base/depth/producer and `SR ctrl +0x08`. The transport already leaves the enable alone - keep it. |
| 8 | H2D id 5 | confirmed forbidden: firmware id-5 handler is a `BUG()` trap, the vendor only sends it on a failure branch. The iface refuses any announce other than id 3. |
| 9 | alg config response carrier | **chip-space group 1 id 3 `hmac_event_config_syn`** (hmac's table). Our alg skeleton cannot register it (it is the hmac port's table); `hi5622_alg_cfg_response()` is the hook that future handler calls. Payload/pairing layout still [U]. |

### 7.2 Transport changes required before B3 (found while reconciling; documented, not applied here)

These are **wifi-transport-port lane work** - this lane did not silently patch
ring logic into its delta.  Full text with evidence:
`transport-delta/README.md` "Required transport changes before B3".

| # | change | why |
| --- | --- | --- |
| T1 | DR consume must advance the host free-buffer index **`DR+0x38`**, not write `+0x3c` (device service index) | `pciet_ete_dr_consume()` writes `HI5622_ETE_CH_B_CNS`; per hcc-re 4.2 the host publishes reclaimed buffers at +0x38, the device owns +0x3c |
| T2 | index encoding `(idx & 0x3ff) \| (phase << 10)`, phase toggles on wrap | current post/consume writes raw indices masked to the ring; any multi-wrap transfer breaks |
| T3 | arm **all seven** channels SR0-2 + DR0-3, depth 32, pre-post DR buffers (B3 P0) | probe currently allocates/programs SR0+DR0 only; without DR free buffers no ready payloads arrive |
| T4 | write `SR ctrl +0x08 = 0` | part of vendor SR init; transport ring_init omits it |
| T5 | if the IRQ path is enabled: mask ETE interrupt block `0x40039508 &= 0xffe0f8f8` | vendor `pcie_ete_intr_init`; the polled path needs nothing |
| T6 | keep channel `+0x00` enable firmware-owned; keep twin/doorbell write form until B3 captures `out[0]/out[2]` | hcc-re warns the vendor H2D announce form may be `out[2] OR bit0`; B2's pending-word form worked, so arbitrate on the bench |

## 8. Unknowns [U] (each with what would close it)

| # | unknown | how it closes | blocks |
| --- | --- | --- | --- |
| U1 | the first **well-formed H2D HCC frame** (group/id/rgroup/payload) that makes the firmware answer (D2H bit 6 `0x40` -> bit 3 ETE-done `0x8`) | B3 candidate matrix via `bench_hcc_probe` (section 10) + hcc-re task-63 | data path, B3 |
| U2 | firmware's expected HCC reply per H2D id (and whether it validates header fields) | B3 dmesg/D2H bitmap capture | U1 |
| U3 | hdpp group 3 (5) and core (7) per-id handler maps | phase25 vocabulary pass / hcc-re | hdpp handlers |
| U4 | alg cfg D2H response id + payload layout | hcc-re / B3 with `alg_protocol=1` | alg bring-up |
| U5 | hdpp TX/RX MSDU ring format (the real data path) | vendor GPL source or deeper RE; hi1105 dpe_ is naming reference only | data path |
| U6 | ETE SR ring-full / back-pressure semantics | bench burst test | throughput path |
| U7 | DR payload wire-length vs 12-byte-header len field consistency | B3 first DR payload | RX validation |
| U8 | band<->RC mapping (endpoint 0 = 2.4G? [I]) | vendor log / D2H payload band bits | per-band netdev |
| U9 | real MAC address provisioning path | hmac/bdinfo RE | netdev identity |
| U10 | INTx/MSI delivery on the luofu RC (polled D2H is the standing design) | host RC/INTx work | latency, not correctness |
| U11 | ack/re-arm writes' effect on the device (safety-verified only by contract, not bench) | B3 first non-empty bitmap with `driver_mode=1`, UART read-only | B3 |

---

## 9. Safety model

* **No device access in this lane.**  The driver never remaps or writes a BAR;
  every device write lives in the transport module.  `attach()` performs host
  allocations, table registration, netdev registration - no device writes.
* **Two independent attended gates:**
  1. `hi5622_pciet.driver_mode=0` (default) keeps the transport's write
     footprint unchanged even with the driver loaded; the iface D2H service
     (ack/re-arm) is enabled only by an explicit `echo 1 > .../driver_mode`.
  2. `hi5622_hdpp_drv.allow_device_writes=0` (default, load-time) keeps the
     ETE channel-register programming (`ops->ete_init`) and the B3 candidate
     frame off; the chip stays attached/deferred instead.
* The never-write register **CA 0x400392f0 (out[5])** is not touched: the
  iface's two write offsets are compile-time `BUILD_BUG_ON`-checked against
  it, and the transport's own gate remains in force.
* **No vendor blobs:** the driver takes the firmware path/blob from the
  transport; the deliverable contains text only.  No calibration data/per-unit
  value anywhere.
* The mock module is hardware-free; the split driver module is loaded only on
  the bench under the recovery door, with UART read-only.

---

## 10. Next steps: B3 - first real HCC exchange on the chip (B2 is verified, contract is in)

**Status input:** transport stage B2 passed 2026-11-22 18:35 (`mem-entries.md`
18:35; transport commit `9700250`): SR0/DR0 programmed
(0x8360c000/0x83634000, depth-1 0x1f), H2D smoke (64 B zero frame + announce
id 3, mask 8) clean, device status live, D2H bitmap still only `0x4`.  The
hcc-re contract (commit `07b9201`) then fixed what the next session must do:
**the ready handshake arrives as HCC chip group-4 payloads on the DR ring, and
nothing must be sent to make it happen**; the missing pieces are (a) all seven
channels armed with pre-posted DR free buffers and (b) the transport-side ring
fixes T1-T4 (section 7.2).

### 10.1 Preconditions

* **Transport lane applies T1-T4 (section 7.2) and rebuilds `hi5622_pciet.ko`**
  (DR free-buffer register +0x38, index phase bits, arm SR0-2+DR0-3 with
  pre-posted DR buffers, SR ctrl=0).  Without T1/T3 the DR payloads cannot
  arrive, so B3 would show only `0x4`.
* Recovery door armed (`tools/recovery-card.sh` READY), bench link PASS, UART
  capture read-only to an absolute path, firmware blob hash matches, no vendor
  modules loaded, one attended session, B2 image (W26a `sha d03a62e5` with
  dma-ranges).
* Driver module built from this deliverable (`tools/build-modules.sh`).

### 10.2 Instrument

```
hi5622_pciet.ko    = transport skeleton + marked integration delta (T1-T4 by that lane)
hi5622_hdpp_drv.ko = this driver skeleton (split build)
```

Driver module parameters for B3:

| parameter | default | meaning |
| --- | --- | --- |
| `allow_device_writes` | 0 | load-time gate for `ops->ete_init` (channel programming) and candidate frames |
| `create_netdev` | 1 | netdev skeleton on/off |
| `bench_chip` | 0 | endpoint index the trigger posts on |
| `bench_hcc_group` / `bench_hcc_id` / `bench_hcc_rgroup` | 0 / 1 / 0 | candidate HCC header values (writable between shots) |
| `bench_hcc_probe` | trigger | **write 1 to post exactly one candidate frame** through the normal SR0+id-3 path; fails with -EAGAIN if the chip is not RUNNING |

Transport parameter `driver_mode` (0 default, writable) switches the D2H
service routine + dispatch to the iface once the driver is attached.

### 10.3 Sequence (each step its own capture, one idea per step)

1. `insmod hi5622_pciet.ko`; run B1/B2 attended steps (`prepare`, `upload`,
   `verify` diffs=0, `release`, `announce`); wait >= 3 s; poll on; confirm the
   bitmap `0x4` announce and live status words.
2. **P0**: arm all rings (SR0-2 + DR0-3, depth 32, pre-posted DR buffers) via
   the transport's extended ringinit; log all bases (0x8xxxxxxx) and depth-1
   (expect 0x1f).
3. `insmod hi5622_hdpp_drv.ko allow_device_writes=1 create_netdev=1`; expect
   attach, D2H table install (11-entry table, vendor set + 2/4), channel
   setup, chip RUNNING, `hiwlan0` registered.  No abort/SError.
4. `echo 1 > /sys/module/hi5622_pciet/parameters/driver_mode`.
5. **P1-P3**: keep polling ~30 s and log every `out[1]`.  Expected: DR
   payloads carrying **chip group 4 id 1 "Device plat ready!"** then **id 2
   "DEVICE READY"** (<= 0x63 B), which complete the driver's group-4 handlers
   and the two vendor waits.  If only `0x4` ever appears: run P4 (read
   `out[0]`, `out[5]` read-only, DR `+0x00` enables) and P5 (node word1 +
   first 16 B per buffer) and stop - that is a finding about T1/T3 or the
   publish side, not a license for extra writes.
6. **First H2D (U1)**: with the chip RUNNING, fire one candidate at a time:
   ```sh
   P=/sys/module/hi5622_hdpp_drv/parameters
   echo 1 > $P/bench_hcc_probe                 # shot A: chip group 0 id 1 (hmac_config_send_event)
   # then set $P/bench_hcc_id to 2 (hmac_config_alg_send_event) and fire again
   ```
   Candidate matrix (hcc-re 6.2/9): chip group 0 id 1, chip group 0 id 2,
   then resource-group nibble variants 0/1/2 for the candidate that reacts.
   Watch for: D2H bit 3 (`0x8`, ETE transfer done - then the driver drains DR
   and logs the HCC frame group/id), bit 6/7 (`0x40`/`0x80`, staged/wake), any
   DR payload, and the driver's hcc dispatch log (space/group/id).  Keep the
   device status words live and no abort.
7. Stop at the first device reaction and capture dmesg + UART + `out1` +
   `status` + `d2h_count`; power-cycle is the only reset after release.

### 10.4 Success criteria / stop rules

* B3 success: the two ready HCC payloads are observed and dispatched (group 4
  ids 1/2 complete), and/or one candidate H2D frame produces a **new** D2H
  bitmap bit and/or DR payload with the device status words still nominal.
* Stop immediately on external abort/SError, stuck doorbell drain, UART panic,
  or a bitmap that keeps re-arming without progress.  **Never write CA
  0x400392f0; never post H2D id 5; UART read-only.**
* The B3 capture is also the arbitration for U1/U2/U5/U7 and the first real
  data for the hcc-re contract corrections.

### 10.5 After B3

* Implement the hmac/wal tables (chip groups 1/2/3 and their 28/12/19 ids from
  hcc-re 5.5) with the config-sync decoder at chip group 1 id 3; that unblocks
  the alg skeleton (`alg_protocol=1` + `hi5622_alg_cfg_response()`).
* Replace the hdpp handler stubs with the hmac/hdpp bodies (object model,
  WOW/rx-schedule/tx-complete/BA paths) and then reverse the MSDU data path
  (U3) so the netdev stops using the HCC control-path placeholder.
* cfg80211/wiphy remains a separate follow-up (needs CONFIG_CFG80211=y and the
  `wal_cfg80211` surface; out of scope here).

## 11. Evidence ledger

| claim | artifact / command | grade |
| --- | --- | --- |
| interface + 3 modules compile clean on 6.18.60 | `tools/build-modules.sh` logs; `BUILD-NOTES.md` sha256s/vermagic; `nm` export check | [V] |
| transport delta applies to the current transport source | `git apply --check -p1` on the baseline sha256 (`transport-delta/README.md`) | [V] |
| driver has exactly the 2 transport exports unresolved | `nm -u hi5622_hdpp_drv.ko | grep hi5622` | [V] |
| B2 ring/H2D pass (device) | `mem-entries.md` 2026-11-22 18:35; transport commit 9700250; `wifi-transport-port/BENCH-PLAN.md` | [V*] |
| 12-byte HCC header + boot dialogue + D2H service order | `phase37/THE-PROTOCOL-MAP.md` 1.2/2.2/3.1/3.2 (byte-verified) | [V] |
| hdpp group 3 = 5 / core = 7 registration sizes and ids | phase37 3.2; wifi-driver-re 3.3; wifi-hcc-re 5.5 | [V] |
| H2D id 5 is a firmware BUG() trap | phase37 1.1 step 3 | [V] |
| ETE ring shape / bases / depth | B2 logs in `mem-entries.md` 18:35; phase42 E.3 | [V*] |
| D2H mailbox registration set {1,3,6,7} | phase37 3.1 | [V] |
| bit-2 meaning resolved: platform-up announce, no vendor handler, real handshake = HCC group 4 ids 1/2 on DR | `wifi-hcc-re/REPORT.md` 0/2.4/5.3; B1/B2 logs | [V]/[V*] |
| cfg80211 not buildable on this tree | `linux-6.18.60/.config`: `# CONFIG_CFG80211 is not set` | [V] |
| post-B1 HCC/H2D/D2H/ETE contract (ready handshake, spaces, hdpp ids, mailbox set, transport ring fixes) | `wifi-hcc-re/REPORT.md` sections 2/4/5/6/9 (task-63, submodule commit 07b9201) | [V] |
| bit-2 meaning / real ready handshake over the DR ring | `wifi-hcc-re/REPORT.md` 0/2.4/5.3 + B1/B2 bench logs | [V]/[V*] |
| DR free-buffer index +0x38 vs device +0x3c, phase-bit index encoding, arm-all-rings | `wifi-hcc-re/REPORT.md` 4.2/4.4/9 | [V] |

## 12. Short version: what compiles, what is stubbed, exact next steps

**What compiles (verified 2026-11-22, module build only, no `make world`):**
* `hi5622_pciet.ko` = transport skeleton + the 6-hunk marked integration delta
  (`interface/pciet_iface.c`) - 142,268 B, exports
  `hi5622_transport_{register,unregister}_client`.
* `hi5622_hdpp_drv.ko` = the hdpp-style driver skeleton - 202,068 B, exactly
  two unresolved `hi5622_*` symbols (the two transport exports, linked via
  `KBUILD_EXTRA_SYMBOLS`).
* `hi5622_hdpp_mock.ko` = driver + simulated transport - 236,508 B,
  self-contained, hardware-free smoke test.
All three: `vermagic=6.18.60 SMP mod_unload ARMv7 p2v8`, GPL, zero cc
warnings under the tree's `-Werror` (only the known
`CONFIG_MODULE_STRIPPED` MODULE_DESCRIPTION modpost note).  Exported symbol
set, unresolved-symbol set and mock self-containment are asserted by
`tools/build-modules.sh`.  Details/hashes: `BUILD-NOTES.md`.

**What is real vs stubbed**
* Real: the whole transport<->driver binding (client ops table, D2H register
  chain, atomicity/lifetime, `driver_mode` gate); the 12-byte HCC header +
  tx queue + 3-space/5-group dispatch with the pinned hdpp ids; mailbox ids
  0..10 + chip-group-4 ready completes; ETE SR0/DR0 service wrapper; netdev
  lifecycle; module state machine; the B3 one-frame trigger.
* Stubs / not yet: hdpp handler bodies (real hmac/hdpp code), hdpp MSDU data
  path (netdev TX rides the HCC control path with a placeholder id), alg
  config protocol (disabled; carrier is chip group 1 id 3), wiphy/cfg80211
  (`CONFIG_CFG80211` is not set on this tree), MAC provisioning.

**Exact next steps (B3; needs the transport lane first):**
1. Transport lane applies T1-T4 from section 7.2 (DR free-buffer index +0x38,
   index phase bits, arm **SR0-2 + DR0-3** with pre-posted DR free buffers, SR
   ctrl=0) and rebuilds the patched transport.
2. Attended session: B1/B2 steps -> arm all rings (P0) -> insmod
   `hi5622_hdpp_drv.ko allow_device_writes=1` -> `echo 1 >` the transport's
   `driver_mode` -> poll ~30 s for **HCC chip group 4 id 1 then id 2** payloads
   on the DR ring (they complete the driver's ready state; **nothing must be
   sent**).  If only bitmap `0x4` appears: P4/P5 dumps, stop.
3. Then fire one candidate H2D config event at a time with
   `echo 1 > /sys/module/hi5622_hdpp_drv/parameters/bench_hcc_probe`
   (defaults chip group 0 id 1; then id 2; rgroup variants), capturing
   `out[0]/out[1]/out[2]/out[3]/out[4]`, dmesg/UART, status words.  Never
   post H2D id 5, never write CA 0x400392f0, UART read-only.
4. Close U1/U2/U5/U7 from that capture, then port the hmac/wal tables + config
   sync decoder (chip group 1 id 3) and the hdpp MSDU data path.

## 13. Reproduce

```sh
# in the repository root (the opensource/ submodule):
opensource/docs/incoming/wifi-driver-integration/tools/build-modules.sh
# artifacts + verification output end up in ~/wifi-driver-port-work/
```
