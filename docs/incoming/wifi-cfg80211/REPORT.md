# wifi-cfg80211 - cfg80211 full-MAC skeleton M0-M2 over the working HCC duplex protocol

Lane: wifi-cfg80211 (shared task **task-104**), host-only, 2026-11-24.
Status: **M0-M2 implemented, compile-clean, host-selftested; not bench-fired by
this lane.**  No device was contacted; no vendor binary, firmware byte,
calibration value, per-unit secret or DTS extraction is reproduced here.  The
PD4 patch image and the Wi-Fi PHY custom block remain local bench inputs under
the project publication rules.

This lane deliberately does not duplicate protocol work: the H2D frames that
still need builders are tracked by **task-103 (wifi-proto-seq)** and referenced
here by group/id/key; the driver model returns -EOPNOTSUPP rather than faking
success while a builder is missing.

---

## 0. TL;DR

* Built **hi5622_wifi.ko** (project 6.18.60 tree, ARM, zero compiler/modpost
  warnings, 504,800 B sha256 7d01e340..., **depends=cfg80211**) from the
  bench-validated wifi-h2d-send driver tree plus a new cfg80211 layer.
* Wiphy is allocated per PCI endpoint, carries the **RE-derived 2.4/5 GHz
  band + channel + cipher capabilities**, and is registered **only after the
  HCC ready handshake (g4 id1 + g4 id2) has been decoded** - the vendor order,
  and the reason the wiphy can never appear before the firmware speaks.
* Real **netdev VAP lifecycle** via add_virtual_intf / del_virtual_intf /
  change_virtual_intf (M0-M1 netdev model).  The firmware user/vap H2D
  messages (g0 id7/id8 add, g0 id9 delete) are not built yet, so the model
  records "hcc add pending" instead of sending a speculative frame.
* M2 ops **get_station / dump_station / scan are registered -EOPNOTSUPP
  stubs**; every M3+ op stays NULL (cfg80211 returns -EOPNOTSUPP by itself).
* Portable host-tested **VAP model + registration gate + 39-op cfg80211 <-> HCC
  message map** (vendor-populated op set from wifi-driver-re sec 2.2).
* Tests: cfg80211 logic **447/447**, inherited H2D logic **242/242**, driver
  source selftest **138/138**; provider-symbols check PASS.
* Handoff: **CONFIG_CFG80211=m must be added to the target kernel config**
  (cfg80211.config; task-107) - the current project tree builds it unset.

---

## 1. What this lane builds on (all hardware-validated)

The full-duplex HCC protocol works on our clean-room host (ledger 2026-11-24
22:10 / 2026-11-25 05:10; STATUS.md sec 3i/3j):

    GPIO reset pulse -> ROM S0/S1 (0xCEED handshake, release 0x5A5A ONLY)
     -> firmware golden state (page CA 0x1BF000 = 0x86807FEF/7/2/0xABCD,
        state CA 0x118B60 = 1, exception mask 0)
     -> device->host ready handshake decoded by the direct device group-A DR
        reader (g4 id1 + g4 id2 payload 0x01060850)
     -> host->device REAL messages: context-create (g0 id1 key 0x64, type 1)
        allocates the context (ctx+0x190), then TXQ config sync (g0 id1 cmd
        0xd8) is consumed with ZERO exceptions, and the firmware RESPONDS with
        g1 id3 (36 B) + g1 id0x12 (40 B) D2H frames.

Driver tree inherited by this lane: ../wifi-h2d-send/src (400,732 B module,
sha256 774c4f1f..., 0 warnings; attrs prepare/upload/verify/seed/hcc_arm/
hcc_loop/release/h2d_ctx_create/h2d_txq_sync72/h2d_send/...).  The PD4 patch
(image sha256 cfe24bfb..., stub in CPU 0x108240) is the current firmware path
for real H2D messages; this lane adds no firmware change and sends no message.

All protocols/frames referenced below are committed in:
../wifi-hcc-core/, ../wifi-d2h-direct/, ../wifi-h2d-frames/,
../wifi-h2d-send/, ../wifi-ctx-create/, ../wifi-irq-unmask/REPORT-PD3.md
and REPORT-PD4.md, ../wifi-vendor-userspace/, ../wifi-driver-re/,
../wifi-h2d-catalog/, and the phase14/phase2 documents cited inline.

---

## 2. Layering and integration points

    userspace:  wpad (hostapd+wpa_supplicant) | netifd | iw/iwinfo
                            | nl80211 / cfg80211
    ==================== kernel =========================================
     hi5622_cfg80211_ops.c   struct cfg80211_ops (M0-M2): add/del/change VAP,
                             -EOPNOTSUPP stubs; VAP netdev ndos (drop-TX stub)
     hi5622_cfg80211.c       wiphy alloc/free, band/channel/cipher capability,
                             ready-gated registration work, RTNL teardown, sysfs
     hi5622_cfg80211_logic.c portable VAP model, registration gate, 39-op map
                             (host-tested, no kernel deps)
     hi5622_hcc/...          working HCC duplex protocol (unchanged): hcc_core
                             state machine, H2D send path, direct D2H reader
     pcie transport          BAR windows, ETE rings, GPIO pulse, upload, release
    ======================================================================
     firmware (vendor blob, PD4-patched at bench; boot/ready already decoded)

Integration points (files changed relative to the wifi-h2d-send tree):

| file | change |
| --- | --- |
| src/main.c | include; hi5622_cfg80211_init(ctx) after the HCC probe; hi5622_cfg80211_remove(ctx) in remove; "cfg80211" sysfs group |
| src/service.c | include; hi5622_cfg80211_ready(ctx) in the service pass (idempotent, cheap, no device write) |
| src/hi5622_driver.h | forward declaration + struct hi5622_cfg80211 *cfg state pointer |
| src/Kbuild | three new objects + KBUILD_EXTRA_SYMBOLS += $(src)/cfg80211-provider.symvers |
| new files | hi5622_cfg80211.{c,h}, hi5622_cfg80211_ops.c, hi5622_cfg80211_logic.{c,h}, cfg80211-provider.symvers |

Nothing else in the inherited protocol tree is modified (selftest checks file
parity with ../wifi-h2d-send/src for every copied protocol file).

Safety boundaries preserved by construction: probe performs no device writes;
the cfg80211 layer performs **no MMIO at all** (no readl/writel); it sends
**no H2D message**; the tested boot/service logic (release-only, custom-block
pre-seed, direct D2H reader, hcc_core policy) is untouched.

---

## 3. Wiphy model

One wiphy per PCI endpoint (the board has two endpoints, one radio each:
0000:00:00.0 / 0001:00:00.0, DRIVER-BLACKBOX sec 1).  The vendor also
registers one wiphy per band (wal_cfg80211_init loops per band,
wifi-driver-re sec 2.3).  A wiphy is allocated in probe (no device access),
then registered from a work item once ready is latched.

**Band selection.**  Auto: PCI domain 0 -> 2.4 GHz, domain 1 -> 5 GHz.  This
mapping is the phase15 reading [I]; the vendor uses is_pcie_cross=1 and the
thermal lane explicitly flagged "domain != band by assumption"
(wifi-thermal-re sec 2.1), so auto is only a default and the band_force module
parameter (-1 auto, 0 = 2.4 GHz, 1 = 5 GHz) exists to make the bench decision
without a rebuild.

**Channel/power plan.**  2.4 GHz: channels 1..13 (2412..2472 MHz), max_power
30 dBm (vendor live readout 30.0 dBm, wifi-driver-re sec 2.3 [V*]); channels
12/13 are left to cfg80211's regulatory core.  5 GHz: the shipped INI LU
custom-regdomain plan from phase2/power-decode.md sec (b): BAND1 5170-5250
(ch 36/40/44/48), BAND2 5250-5330 (ch 52/56/60/64, DFS+indoor flags), BAND3
5490-5710 (ch 100..140, DFS+indoor flags) = 19 channels; max_power 23 dBm
(vendor live 23.0/24.0 dBm).  Channel 144 and 149-196 are outside the LU plan
and are NOT advertised.  The 160 MHz operation observed live on ch 44 is noted
for M3+ (HT/VHT/HE capability tables come from the INI then); M0-M2 advertises
an HT20 skeleton (SGI20 + AMSDU, MCS 0-7, 12 legacy rates 2.4 GHz / 8 rates
5 GHz - the same baseline as the phase14 wiphy work).

**Ciphers / modes.**  Standard suites for stock hostapd: WEP40, WEP104, TKIP,
CCMP, GCMP, AES-CMAC, BIP-CMAC-256.  interface_modes = STATION | AP | MONITOR
(vendor-relevant AP/STA/monitor; WDS cannot be advertised at all on 6.18 -
wiphy_register() WARNs on BIT(NL80211_IFTYPE_WDS) - and NAN would require NAN
ops; AP_VLAN needs the parent-VAP plumbing of M3).  max_scan_ssids = 12
(vendor live value [V*]).  perm_addr is a documentation-purpose
locally-administered address (02:00:68:69:35:00); VAP addresses derive from it
and are never a per-unit MAC.

**Registration gate** (the important safety property): the wiphy is registered
only when hcc_core reports boot_latched AND phase >= READY_TXQ_SYNC AND both
ready-pair messages were decoded (g4 id1 plat_ready + g4 id2 device_ready).
The gate is a pure function in the host-tested model
(hi5622_cfg_reg_gate), so the transition matrix is unit-tested.  Registration
runs in a workqueue, never in the service thread; remove() sets removing,
cancels the work, deletes every VAP netdev under RTNL, then
wiphy_unregister() + wiphy_free().  If wiphy_register() ever fails, the module
stays loaded and the boot/service path is unaffected (logged error only).

---

## 4. VAP model and cfg80211 op semantics (M0-M2)

**M0 - netdev model.**  add_virtual_intf validates the type through the
portable model (AP/STA/MONITOR; AP_VLAN/WDS/P2P rejected -EOPNOTSUPP), derives
the VAP address, allocates the netdev with the wireless_dev in its private
area, and registers netdev+wdev in one call (cfg80211_register_netdevice(),
RTNL+wiphy-mutex context supplied by cfg80211).  netdev ndos are open/stop
success plus a **drop-stub ndo_start_xmit** - without it bringing an interface
up oopses in the IPv6 stack (phase14 wifidrv0 lesson).  The wdev lives inside
the netdev priv and is freed by the netdev core after unregister
(needs_free_netdev); the del path must not free it because cfg80211's
post-doit still reads wdev->netdev (this is why the inline
cfg80211_unregister_netdevice() - compiled empty when the build config has
CONFIG_CFG80211 unset - is avoided in favour of the unconditional exported
cfg80211_unregister_wdev()).

**M1 - VAP lifecycle.**  del_virtual_intf unregisters the wdev/netdev and
drops the model slot.  change_virtual_intf accepts a same-type no-op and
returns -EOPNOTSUPP for real type changes/4-address changes (the vendor's WDS
4-address behaviour needs the g0 id7/8/9 user/vap messages and is M3+).
Each created VAP is recorded as **hcc add pending**: the netdev model works
(M0 host gate) but no firmware VAP object exists until the user-add builder
lands.  The status file says so explicitly.

**M2 - clean stubs.**  get_station, dump_station, scan are registered and
return -EOPNOTSUPP (and count the hit in the status).  They never return 0:
userspace stays honest until the scan request (g0 id0xC, 508 B payload,
currently [U]) and the D2H scan results (g1 ids 15/16/17, g2 id2) or the
station-event frames (g2 ids 3/4/5/6) are decoded [I].

Every M3+ op is left NULL in struct cfg80211_ops, so cfg80211 itself rejects
it with -EOPNOTSUPP without a driver callback.

---

## 5. cfg80211 op <-> HCC message mapping table

Source of truth (machine-readable, host-tested): src/hi5622_cfg80211_logic.c
(hi5622_op_map[], 39 rows = the vendor's populated op set,
wifi-driver-re sec 2.2).  Group/id/key columns are the H2D contract; D2H
columns are the expected response/event frames.  "task-103" marks the frame
builders that are still open (wifi-proto-seq delivers them; this lane only
references them).

| # | cfg80211 op | milestone | M0-M2 state | H2D (group/id) | D2H / events | builder |
| ---: | --- | --- | --- | --- | --- | --- |
| 1 | add_virtual_intf | M1 | implemented (netdev) | g0 id7 hmac_user_add (168 B), g0 id8 notify-alg | g1 id3 sync / user events | **pending task-103** |
| 2 | del_virtual_intf | M1 | implemented (netdev) | g0 id9 hmac_del_user_notify_dmac (12 B) | g1 id3 | exact builder exists |
| 3 | change_virtual_intf | M1 | implemented (same-type only) | - | - | n/a |
| 4 | add_key | M3 | deferred | g2 WAL cfg event (id [U]) | g1 id3 | pending |
| 5 | get_key | M3 | deferred | g2 WAL cfg event | - | pending |
| 6 | del_key | M3 | deferred | g2 WAL cfg event | - | pending |
| 7 | set_default_key | M3 | deferred | g2 WAL cfg event | - | pending |
| 8 | set_default_mgmt_key | M3 | deferred | g2 WAL cfg event | - | pending |
| 9 | start_ap | M3 | deferred | g0 id1 hmac_config family + g2 WAL cfg | g1 id3, beacons | pending (G1) |
| 10 | change_beacon | M3 | deferred | g2 WAL cfg event | - | pending |
| 11 | stop_ap | M3 | deferred | g2 WAL cfg event | - | pending |
| 12 | add_station | M3 | deferred | g0 id7 user-add family | g2 3/4/5/6 | pending |
| 13 | del_station | M3 | deferred | g0 id9 | - | exact builder exists |
| 14 | change_station | M3 | deferred | g2 WAL cfg event | - | pending |
| 15 | get_station | M2 | **stub -EOPNOTSUPP** | - | g2 ids 3/4/5/6 | pending |
| 16 | dump_station | M2 | **stub -EOPNOTSUPP** | - | g2 ids 3/4/5/6 | pending |
| 17 | change_bss | M3 | deferred | g2 WAL cfg event | - | pending |
| 18 | scan | M2 | **stub -EOPNOTSUPP** | g0 id0xC hmac_enc_scan_send_req (508 B) | g1 15/16/17, g2 id2 | **pending task-103** |
| 19 | connect | M5 | deferred | g0 id0xF / 0x10 / 0x11 join family | g2 3/4/5/6 | pending |
| 20 | disconnect | M5 | deferred | g0 id0x10 family | g2 3/4/5/6 | pending |
| 21 | set_wiphy_params | M4 | deferred | g0 id1 hmac_config family | g1 id3 | pending |
| 22 | set_bitrate_mask | M4 | deferred | g2 WAL cfg event | - | pending |
| 23 | dump_survey | M2 | deferred (no stub) | scan result path (g0 id0xC) | g1 15/16/17 | pending |
| 24 | set_pmksa | M3 | deferred | g2 WAL cfg event | - | pending |
| 25 | del_pmksa | M3 | deferred | g2 WAL cfg event | - | pending |
| 26 | flush_pmksa | M3 | deferred | g2 WAL cfg event | - | pending |
| 27 | remain_on_channel | M5 | deferred | mgmt/offchannel family [U] | - | pending |
| 28 | cancel_remain_on_channel | M5 | deferred | mgmt/offchannel family [U] | - | pending |
| 29 | mgmt_tx | M5 | deferred | mgmt TX family [U] | mgmt status | pending |
| 30 | mgmt_tx_cancel_wait | M5 | deferred | mgmt/offchannel family [U] | - | pending |
| 31 | set_power_mgmt | M6 | deferred | mailbox id1 PM sleep (hcc_core_pm_request_sleep), PM disabled on boot path | g3 id17 PM event | core helper exists |
| 32 | update_mgmt_frame_registrations | M6 | deferred | - (local bookkeeping) | - | n/a |
| 33 | sched_scan_start | M5 | deferred | scan family g0 id0xC | g1 15/16/17 | pending |
| 34 | sched_scan_stop | M5 | deferred | scan family | - | pending |
| 35 | get_channel | M6 | deferred | - (local channel state) | - | n/a |
| 36 | start_p2p_device | M6 | deferred | - | - | vendor stub |
| 37 | stop_p2p_device | M6 | deferred | - | - | vendor stub |
| 38 | set_mac_acl | M6 | deferred | g2 WAL cfg event | - | pending |
| 39 | external_auth | M6 | deferred | g2 WAL cfg event / external auth family | - | pending |

Counts enforced by the host selftest: 39 rows, 3 implemented (all M1), 3
registered -EOPNOTSUPP stubs (M2), 33 deferred; 7 rows in M0-M2 scope, the rest
M3-M6.

### 5.1 HCC message reference table (task-103 handoff view)

src/hi5622_cfg80211_logic.c hi5622_msg_refs[] (19 rows) records every frame
this layer's map touches:

| frame | dir | group/id | len | builder status | milestone |
| --- | --- | --- | ---: | --- | --- |
| ctx_create (key 0x64) | H2D | g0 id1 | 0x14 | exact (h2d_build_ctx_create), proven on hardware | M1 precondition |
| txq_sync (cmd 0xd8) | H2D | g0 id1 | 0x14 | exact (h2d_build_txq_sync), proven on hardware | M1 |
| user_add | H2D | g0 id7 | 168 | **pending task-103** | M1 |
| user_add_notify_alg | H2D | g0 id8 | 168 | **pending task-103** | M1 |
| del_user | H2D | g0 id9 | 12 | exact (h2d_build_del_user) | M1 |
| scan_req | H2D | g0 id0xC | 508 | **pending task-103** (payload [U]) | M2 |
| sta_join | H2D | g0 id0xF | - | unknown, task-103 | M5 |
| sta_wait_join | H2D | g0 id0x10 | - | unknown, task-103 | M5 |
| sta_join_result | H2D | g0 id0x11 | - | unknown, task-103 | M5 |
| edca_opt_timeout | H2D | g0 id0x1D | 60 | exact (h2d_build_edca_opt) | M3 |
| WAL cfg event | H2D | g2 | - | unknown command ids [U] | M3 |
| config_syn (response) | D2H | g1 id3 | 36 | observed on bench, decode pending task-103 | M1 |
| g1 id0x12 (response) | D2H | g1 id0x12 | 40 | observed on bench, decode pending task-103 | M1 |
| scan bss / complete / channel | D2H | g1 id15/16/17 | - | vendor path [I] | M2 |
| scan rsp (sta) | D2H | g2 id2 | - | vendor path [I] | M2 |
| station events | D2H | g2 ids 3/4/5/6 | - | asoc/connect/disconnect [I] | M3 |
| PM event | D2H | g3 id17 | - | hmac_d2h_pm_event | M6 |

The exact H2D payloads remain task-103's deliverable: this lane references
group/id/key/length only and never synthesizes a payload it cannot justify.

---

## 6. Milestones

This lane's task definition (M0 wiphy+netdev, M1 VAP create/destroy, M2 scan)
refines the vendor-userspace lane's M0-M7 plan
(../wifi-vendor-userspace/REPORT.md sec 8: M0 transport/ready, M1 HCC message
service, M2 cfg80211 shell, M3 AP, M4 data path, M5 scan/STA, M6 calibration,
M7 vendor compat).  Mapping: vendor M0/M1 are the inherited working protocol
and driver tree; vendor M2 is split here into **M0 (wiphy+netdev)** and
**M1 (VAP lifecycle)**; vendor M5 scan is advanced into **M2** as stubs so
userspace has a defined answer early; vendor M3/M4/M5/M6/M7 line up with this
lane's M3/M4/M5/M6.

| milestone | gate | state |
| --- | --- | --- |
| M0 wiphy+netdev | compile + host selftest; bench: wiphy appears after ready, iw phy / iw dev enumerate | **delivered host-side** (bench gate pending the cfg80211 kernel config) |
| M1 VAP create/destroy | netdev VAP add/del through nl80211; firmware VAP message once g0 id7/8 builder exists | **netdev half delivered; HCC half blocked on task-103** |
| M2 scan/station stubs | scan/get_station/dump_station return -EOPNOTSUPP cleanly | **delivered** |
| M3 AP control | start/stop/beacon/keys/stations -> four APs enabled equivalent | planned: g2 WAL cfg payloads + g0 id7/8/9, station D2H decode |
| M4 data path | hdpp queues + netdev TX/RX, first DHCP/ICMP through an AP | planned (datapath lane) |
| M5 scan/STA/mgmt | iw scan returns BSSes; STA joins a test AP; mgmt TX | planned: g0 id0xC/0xF/0x10/0x11 + g1 15/16/17/g2 2 + mgmt family |
| M6 cal/power/extras | factory calibration application, INI regdomain, PM, optional extras | planned (calibration/regulatory lane) |

---

## 7. Verification (all device-free)

| step | command | result |
| --- | --- | --- |
| host cfg80211 logic selftest | tools/build.sh step 1 | **447/447 PASS** |
| host inherited H2D logic selftest | tools/build.sh step 2 | **242/242 PASS** |
| kernel module build | project 6.18.60 tree, -Werror, modpost | rc=0, **0 warnings** |
| artifact | modinfo / sha256sum / nm | 504,800 B, sha256 7d01e340..., vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, depends=cfg80211, exactly 6 wiphy_*/cfg80211_* imports |
| provider symbols | tools/gen_cfg80211_symvers.py --check | PASS: 6 symbols, all real net/wireless exports, none tree-provided |
| driver source selftest | tools/selftest.py | **138/138 PASS** |

The module hash is build-path dependent (debug info records the build
directory); record the hash of the build you deploy.

### 7.1 The cfg80211-unset tree quirk (why the provider file exists)

The project 6.18.60 tree builds with "# CONFIG_CFG80211 is not set" (OpenWrt
ships cfg80211 as the kmod-cfg80211 package) but with
CONFIG_CFG80211_HEADERS=y, which is exactly the upstream 6.18 header-support
config: the types/structs/prototypes are available for out-of-tree compile,
and struct net_device keeps its ieee80211_ptr field
(#if IS_ENABLED(CONFIG_CFG80211_HEADERS)).  Two consequences, both handled:

1. **modpost** cannot resolve the six cfg80211 exports from the tree's
   Module.symvers, so the lane declares them in src/cfg80211-provider.symvers
   (KBUILD_EXTRA_SYMBOLS; the same technique the project already uses in
   ../wifi-wal-glue/glue/).  The generator tool derives the required set from
   the actual built objects, minus what the tree provides, intersected with
   the real EXPORT_SYMBOL list parsed from <KDIR>/net/wireless/*.c; it also
   rewrites the file with --update.
2. **Header inlines gated on IS_ENABLED(CONFIG_CFG80211)** (notably
   cfg80211_unregister_netdevice()) would compile to nothing on this tree and
   then be wrong on the target kernel.  The driver therefore calls the
   unconditional exported cfg80211_unregister_wdev() directly, and the
   selftest asserts the gated inline is not used.

The target kernel still needs CONFIG_CFG80211=m for the module to load:
that is cfg80211.config, a handoff to the target/defconfig lane (task-107),
not a change made here (it is outside this lane's write scope).

---

## 8. Bench gate (attended only; not executed by this lane)

This lane's deliverables are host-only.  When the bench is scheduled (after
the cfg80211 kernel config and the task-103 VAP builders exist), the M0-M2
observations are read-only for this layer:

    0. attended session, recovery door armed; PD4 golden boot as documented
       (release 0x5A5A only, never 0xCECE during boot; no rmmod after release)
    1. insmod hi5622_wifi.ko (this lane's build) -> probe allocates the wiphy,
       no device writes, no H2D message
    2. prepare / upload / verify / seed / hcc_arm / hcc_loop / release
    3. wait for golden page + ready pair (core phase READY_TXQ_SYNC/ACTIVE)
       -> expect dmesg: "cfg80211: ready handshake decoded ... scheduling wiphy
          registration" then "cfg80211: wiphy registered after the ready handshake"
    4. iw phy                          # new clean wiphy next to the vendor phys
       iw phy <phy> info               # band/channels/ciphers from sec 3
    5. iw phy <phy> interface add cfgtest type managed
       iw dev                           # VAP enumerated; cfg80211_status shows
                                        # "hcc-add=pending" (no H2D sent)
       iw phy <phy> interface add cfgtest-ap type AP
       iw dev cfgtest-ap del           # del_virtual_intf path
    6. stop: one module load, no retries, no rmmod after release;
       /sys/.../cfg80211_status is read-only evidence.

Stop rules: no H2D message from this layer; no device write from this layer;
if the wiphy does not register, capture dmesg + cfg80211_status and stop
(no retry loop); no rmmod after release per the project rules.

---

## 9. Integration diff (design decisions worth review)

1. **wdev in netdev priv** (not a free-standing wireless_dev): required so
   cfg80211's post-doit dev_put(wdev->netdev) cannot hit freed memory; the
   netdev core frees it after unregister (needs_free_netdev).
2. **Ready-gated registration in a workqueue**: keeps the service thread
   non-blocking and matches the vendor order (wiphy only after ready).  The
   gate is a pure function and unit-tested.
3. **No fake success**: M2 stubs return -EOPNOTSUPP and the VAP model marks
   the missing firmware message explicitly.  This is deliberate - a userspace
   stack that trusts a fake start_ap/scan would hide the real gap.
4. **No direct device access in the cfg80211 layer**: the only device-touching
   code remains the inherited transport/HCC files; the cfg layer uses no
   readl/writel (selftest enforced).
5. **One wiphy per endpoint with band_force**: the domain->band mapping is
   [I]; making it a parameter avoids baking an unproven assumption into the
   bench plan.

---

## 10. Risks / open items

| # | risk | impact | mitigation |
| --- | --- | --- | --- |
| R1 | CONFIG_CFG80211 unset in the target tree | driver cannot load on the bench image | cfg80211.config handoff to task-107; bench waits for it |
| R2 | VAP firmware messages (g0 id7/id8 add) still unbuilt | no firmware VAP object, so no AP/data path yet | task-103 builders; the driver never sends a speculative frame |
| R3 | scan payload (g0 id0xC, 508 B) still [U] | M2 scan is a stub | same handoff; D2H result ids known (g1 15/16/17, g2 2) |
| R4 | domain->band mapping unproven (cross-wiring) | wrong band per endpoint if auto is wrong | band_force parameter + loud log; bench decision |
| R5 | 5 GHz channel flags from a hypothesis (INI LU plan) | regulatory/DFS behaviour may differ | encoded from phase2/power-decode.md; custom regdomain port is M6 |
| R6 | two endpoints both register wiphys | two wiphys in iw phy (vendor also has two) | expected vendor-parity; naming/band from band_force |
| R7 | cfg80211 registration failure path | no userspace interface, but boot stays golden | logged error, no retries, module stays loaded |
| R8 | stock netifd/hostapd flow | hostapd start_ap is M3 | M0-M2 gate is enumeration + VAP lifecycle only |

---

## 11. Provenance and regeneration

Everything in this lane is reproducible device-free:

    ./tools/build.sh                                  # host + kernel + selftests
    ./tools/gen_cfg80211_symvers.py --check \
        --kdir <6.18.60 tree> --src src               # provider symbols
    python3 tools/selftest.py --kdir <6.18.60 tree>   # source selftest

Inputs referenced (all committed under opensource/docs/incoming/):
wifi-h2d-send (driver tree + H2D send path), wifi-hcc-core (portable protocol
core), wifi-d2h-direct (direct receive path), wifi-ctx-create (g0 id1 key 0x64
context-create), wifi-h2d-frames (exact builders + catalogue),
wifi-irq-unmask/REPORT-PD3.md + REPORT-PD4.md (firmware patch path),
wifi-vendor-userspace (cfg80211 scope + startup order), wifi-driver-re
(cfg80211 ops map + wiphy registration), wifi-h2d-catalog (H2D tables),
phase14 (wiphy/netdev skeleton lessons), phase2/power-decode.md (5 GHz LU
channel plan), DRIVER-BLACKBOX (two endpoints, live wiphy readouts).

License: GPL-2.0 original clean-room code.  No vendor source, object code,
firmware byte, calibration value, secret or per-unit identity is included.
