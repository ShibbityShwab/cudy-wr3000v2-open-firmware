# wifi-power-perf - PM path RE, safe subset, and the TX/scan/poll performance model

**Lane:** wifi-power-perf (shared task-160), Cudy WR3000 v2.0 Hi5622V100
clean-room Wi-Fi project.  **Host-only:** this lane ran host tests, host
models and a host cross-build; it never contacted the bench router.  The
attended ladders in [LADDER-BENCH.md](LADDER-BENCH.md) are for the Lead to
fire.  **Date:** 2026-11-25.  **Base:** FINAL association module (56 params).

## 0. TL;DR

1. **The vendor's power-management path is mostly DEAD on this board.** The
   platform sleep handshake exists (H2D group-0 id1 -> <=500 ms ack -> BAL
   close + GPIO level + wake work -> BAL reinit), but its arming vote
   (`g_wlan_pm_info+0x124`, written only by `plat_pm_vote()`) has **no
   importer in any vendor module**; the only hipriv `pm_switch` command is in
   `wifi_debug.ko`, which the boot script loads **only if /etc/hi_version
   contains "turbo"** (stock 2.4.15/2.5.24 do not); `low_power_init()` is a
   printk stub; the stock Wi-Fi INI has **no PM keys**.
2. **What the vendor actually does at runtime**: per-VAP chip power gating
   (`hmac_config_pwr_on/off` <-> `wlan_pm_open/close` -> `wlan_power_on/off`
   with RF-cal + firmware download) and, when userspace asks, cfg80211 STA
   power-save config (event 0x3e3 / sync cmds 0x1ee+0xe8).  Our stack keeps
   the chip **always-on** and PM **off** (the documented `hcc.c` default).
   **No PM-off message exists on the boot path**, so there is nothing safe to
   add at boot.
3. **PCIe ASPM is disabled by design on this board** (the stock vendor DTS has
   no `aspm-enable`, so the vendor RC skips `hi_pcie_enable_aspm`; public
   Hi1105 guidance also says "avoid L0s and L1").  The safe subset asserts
   vendor parity at probe and offers a one-way `aspm_disable`; it never
   enables ASPM.
4. **Safe subset implemented** (4 new params, +5,520 B stripped module,
   60 params): default-off, gated PM-sleep request + observability; ASPM
   parity assertion; adaptive idle back-off of the 5 ms service poll
   (200 -> 50 wakeups/s/endpoint when idle); TX-completion rearm tunable.
   Plus a portable logic header with **73/73 host checks** and an independent
   performance model that cross-checks the report numbers.
5. **Performance result: the shipped stack is air-bound, not poll-bound.**
   16 in-flight frames x (1000/5 ms) = 3200 frames/s = 2.46 MB/s MPDU =
   18.8 Mbps of 736 B payload; the non-aggregated 2.4 GHz MCS7 air ceiling is
   ~3436 frames/s (~20.2 Mbps payload).  The host window is within 6 % of the
   air ceiling; raising the completion poll alone buys nothing at 2.4 GHz.
   The real levers are **aggregation** (not implemented in this stack),
   **scan batching** (already default: 6-channel requests, 13 channels
   structurally blocked at the DMAC), and **client-side DTIM** (AP power
   unchanged: the chip is always-on).

## 1. Method and evidence policy

- Vendor evidence is derived from the operator-local vendor modules
  (`hi5622v100_plat.ko`, `hi5622v100_wifi.ko` from the stock rootfs; paths
  under /home/... are local evidence and are **not committed**).  The
  regeneration tool is [tools/vendor_pm_disasm.py](tools/vendor_pm_disasm.py);
  it prints function bodies annotated with the .rodata log strings using the
  exact ELF relocation table, so every claim below names a symbol, an address
  and (where useful) the vendor's own log text.
- **No vendor bytes, firmware or disassembly text are committed.**  Only
  derived facts (addresses, ids, behaviour) are in this report, per the
  project publication rule.
- Grades: **[V]** decoded from the named vendor artifact, **[V*]** measured on
  the bench and cited to a tracked record, **[I]** inference from the two.
- The vendor modules in `/tmp` are rebuildable only through the local
  recovery-extract copy; the evidence commands in section 8 say exactly which
  file was read.

## 2. Vendor PM path (static RE)

### 2.1 The platform half - `hi5622v100_plat.ko`

| symbol | addr | what it does (vendor strings in quotes) |
| --- | ---: | --- |
| `plat_init_bal_hcc_excp` | 0xe3c8 call | calls `wlan_pm_init` during BAL/HCC init [V] |
| `wlan_pm_init` | 0xed30 | alloc_workqueue; `request_threaded_irq(82, "d2h_gpio_intr", wlan_pm_d2h_gpio_irq_callback)`; `bal_msg_register(chip 0, id 4, wlan_pm_slp_disallow_callback)`; `bal_msg_register(chip 0, id 5, wlan_pm_wkup_dev_ok_callback)`; `hcc_timer_register(..., wlan_pm_try_to_sleep, 100 ms-ish poll [I])` [V] |
| `wlan_pm_try_to_sleep` | 0xef0 | timer callback; **requires `g_wlan_pm_info+0x124 == 1`**; queues the sleep work; waits 100 jiffies; state `+8==2` = success ("try_to_sleep SUCC"), else retry counter ("try_to_sleep fail::cnt->%u ret = %x") [V] |
| `wlan_pm_sleep_work` | 0xfdc | completes the GPIO-cfg wait ("wlan_pm_gpio_cfg done"); `bal_msg_send(0, 1)` = **H2D group-0 id1**; `wait_for_completion_timeout(40, 50 jiffies)` -> "wait for st_slp_ack_event timeout!"; if `pm+0x194 == 0` -> "dev disagree sleep, but GPIO level is low!"; `bal_close(0)` -> "pcie_power_down:: cost->%u"; `wlan_pm_h2d_gpio_level_set(1)` then `(0)`; "d2h_gpio_irq_trigger::%u"; complete; "wlan_pm_sleep_work time::cost->%u"; "host sleep work is finished!"; error path "wlan_pm_mode_restore!" [V] |
| `wlan_pm_slp_disallow_callback` | 0xe700 | D2H id4 handler: clears `pm+0x194`, completes `st_slp_ack_event` ("slp disallow callback done!") [V] |
| `wlan_pm_d2h_gpio_irq_callback` | 0xe734 | GPIO IRQ 82: completes `st_slp_ack_event`, sets `pm+0x194=1` ("d2h falling gpio irq callback done!") [V] |
| `wlan_pm_wakeup_work` | 0xe8a0 | `h2d_gpio_level_set(0)` then `(1)`; 50-jiffy wait on `st_wkup_ack_event`; **3 attempts**; `bal_reinit(0,0)`; 50-jiffy wait on `st_wkup_dev_ok_event`; "wlan_pm_mode_restore!"; cost logs [V] |
| `plat_pm_wakeup` | 0xea68 | if `pm+8 == 2` (sleeping): queue wake work, 150-jiffy wait on `st_wkup_result_event`; else "dev isn't sleeping, needn't wake up again::%d!"; "the worker is processing, needn't submit again!" [V] |
| `wlan_pm_open` / `wlan_pm_close` | 0xe4c / 0xe7a8 | refcount (`+0x190`), then `wlan_power_on` / `wlan_power_off` [V] |
| `wlan_power_on` / `wlan_power_off` | 0xe46c / 0xe630 | BAL init + `hwifi_rf_cali_file_load` + `firmware_download_function` + timing logs; off = "wlan_power_off FAIL!" path [V] |
| `low_power_init` | 0xe00 | prints "[%s]low_power_init: success", returns 0 - **a stub** [V] |
| `plat_pm_vote` | 0xe6f0 | the only writer of `g_wlan_pm_info+0x124` (`str r0,[r3,#292]`); **no caller in plat.ko and no importer in any vendor module** [V] |

**The gate is dead.**  We searched every vendor module's symbol/string table:
`wifi_debug.ko` and `plat_debug.ko` import `wlan_pm_open/close`,
`wlan_pm_try_to_sleep` and `plat_pm_wakeup` (debug-only entry points), but
**nothing imports `plat_pm_vote`**.  The stock boot script
(`rootfs-2.4.15/usr/bin/wifi_init.sh`, shuangta branch) loads
`hi_kwificlk.ko -> hi_pcie.ko -> hi5622v100_plat.ko -> hi5622v100_wifi.ko`
and only loads `wifi_debug.ko` when `/etc/hi_version` contains "turbo";
stock `hi_version` (luofu_V200 / ChenTang_1.2.16, release) does **not**.
Therefore the timer cannot arm the sleep work at runtime, and the whole
BAL-close/GPIO handshake never runs on a stock box.  [V]

### 2.2 The MAC half - `hi5622v100_wifi.ko`

| symbol | addr | what it does |
| --- | ---: | --- |
| `hmac_config_pwr_on` | 0x15b13c | per-chip power gate: if the VAP-count power-off flag is set and `hmac_get_chip_vap_num()==0`, then `wlan_pm_is_poweron()` -> `wlan_pm_open()` -> `hal_chip_init` + `hmac_txq_table_init` + traffic-control init; "wlan power on !!" [V] |
| `hmac_config_pwr_off` | 0x15b340 | `wlan_pm_close()` (chip power down) [V] |
| `wal_dfr_power_down_dev` | 0x1291d4 | sets the per-VAP power flags, calls `wlan_pm_close()`, wakes the DFR waitqueue; "wal_dfr_power_down_dev:wlan_pm_close succ." [V] |
| `hmac_config_pm` | 0x160780 | forwards WAL PM config to the MAC as hmac event **0x3e3** [V] |
| `hmac_config_set_sta_pm_on` | 0x160eb8 | STA PS mode byte (2 = latch mode 1 at vap+0xed0; 3 = 0xff; else derived), then two `hmac_config_sync_cmd_common` calls: **cmd 0x1ee** (1-byte mode) and **cmd 0xe8** (PM param); "sta_pm sta_pm mode[%d]fail" [V] |
| `hmac_config_set_pm_by_module` | 0x1610a8 | validates pm_module/pm_enable, calls set_sta_pm_on(mode 2); "pm_module = %d, pm_enable = %d" [V] |
| `hmac_config_pcie_pm_level` | 0x575d0 | forwards the PCIe PM level as hmac event **0x283** [V] |
| `hmac_d2h_pm_event` | 0xb36fc | device->host PM event handler: logs band/pm counters and returns; **no host action** [V] |
| `wal_cfg80211_set_power_mgmt` | - | cfg80211 .set_power_mgmt -> WAL cfg msg; "fail to send pm cfg msg" [V] |
| `wal_config_set_pm_switch` | 0x12aea4 | `mov r0,#0; bx lr` - a **stub** (hipriv pm_switch is compiled out in this build) [V] |
| `wal_config_pcie_pm_level` | 0x12b1f0 | real -> `hmac_config_pcie_pm_level` [V] |

**Dispatch caveat.**  `hmac_config_pwr_on/off` are not referenced by any
direct call or relocation we could resolve in the static image (checked with
`readelf -r` and a full symbol scan); the WAL->HMAC config-event dispatch
therefore cannot be followed by symbol alone.  This does not change the
conclusion - the only *static* caller of `wlan_power_on` is `wlan_pm_open`,
and `wlan_pm_open` is imported by `hi5622v100_wifi.ko` - but we grade the
exact boot-time caller chain [I], not [V].

### 2.3 Enable gates: why nothing fires on this board

| gate | stock value | consequence |
| --- | --- | --- |
| platform PM vote `g_wlan_pm_info+0x124` | written only by `plat_pm_vote()`; no importer | sleep timer cannot arm [V] |
| `wifi_debug.ko` (only `pm_switch` userspace path) | loaded only if `/etc/hi_version` has "turbo"; stock does not | no userspace PM vote [V] |
| `low_power_init()` | printk stub | no low-power init [V] |
| stock Wi-Fi INI `cfg_hi5622v100_hisi.ini` | only `rf_pll_ppm_correct/threshold` | no PM config keys [V] |
| PCIe "aspm-enable" DT property | absent in the stock vendor DTS (`pcie-seq/vendor-pcie-sequence.md` sec 6 step 14) | `hi_pcie_enable_aspm` skipped; LNKCTL ASPM bits stay 0 [V] |
| cfg80211 STA PS | only when wpa_supplicant asks | AP mode never uses it [V] |

### 2.4 Vendor runtime vs our always-on state

| behaviour | vendor (stock, no debug module) | ours | gap |
| --- | --- | --- | --- |
| chip power | per-VAP power gating (pwr on/off handlers); power-on does firmware download + RF cal | powered once by transport init, stays on until module unload | **we cannot power-cycle the chip**: our transport is a one-shot upload+release and the firmware refuses a second boot (a power-cycle is the only reset) |
| platform sleep | handshake implemented but **not armed** on this board | not implemented (no BAL close/reinit, no GPIO wake, no rfcal reload) | parity in practice; full implementation would need a different transport lifecycle |
| 802.11 PS | per-VAP STA PS config via MAC sync cmds 0x1ee/0xe8 (userspace-driven) | not implemented (cfg80211 report: `set_power_mgmt` deferred) | missing STA feature, not needed for AP proof |
| PCIe ASPM | off (board DTS) | off (same DTS shape); now asserted at probe | parity, asserted |
| service polling | IRQ-driven (`request_threaded_irq`, vendor INTx) | 5 ms polled kthread per endpoint | +200 wakeups/s/endpoint while idle; the safe subset adds a 20 ms idle cadence |
| heartbeat | armed by vendor boot? | off on our boot path (vendor-matching) | none proven |

## 3. Risk matrix: what the safe subset does NOT do

| idea | why risky | verdict |
| --- | --- | --- |
| send H2D g0 id1 PM sleep at boot/start | the vendor follows it with `bal_close` + GPIO handshake; the device may stop serving the host, and our stack has no GPIO wake or BAL reinit. Recovery = physical power-cycle | **not on the boot path**; default-off one-shot attribute only, ladder P5 (attended, last) |
| implement per-VAP chip power gating | requires re-running firmware download/CPU release, RF cal reload, EEPROM/seed replay; the vendor refuses a second boot after release | out of scope |
| enable PCIe ASPM L1 | vendor disables; public Hi1105 quirk avoids L0s/L1; link stability experiment on an AP we can only recover physically | not offered (one-way disable only) |
| STA power-save config (sync cmds) | MAC sync-command channel not implemented; firmware cooperation unproven | out of scope (cfg80211 M6) |
| raise the TX in-flight pool above 16 | host pool is DMA-slab sized and completion bookkeeping assumes it; ring depth is 64 | ladder-only experiment (P3), not default |

## 4. Safe subset implementation (patch 0001)

New file `hi5622_pm_perf_logic.h` (portable; shared by the module and the
host selftest) plus four small integration edits.  Defaults keep the shipped
behaviour byte-for-byte except for the idle poll back-off, which is inert
during traffic and in the pre-ACTIVE boot phase.

| change | default | safety argument |
| --- | --- | --- |
| `pm_enable` module param | **0** | the PM message is behind `hcc_core_pm_request_sleep`, which itself requires enabled + boot-latched + ACTIVE; at 0 nothing can send H2D id1 |
| `pm_sleep_ack_ms` | 500 | clamped 50..5000; vendor window is 50 jiffies at HZ=100 |
| `hcc_pm_sleep` WO attr | refuses unless `pm_enable=1` | one-shot gated; every refusal reason is logged; no retry loop |
| `hcc_pm_status` RO attr | - | observability: gate inputs, request/disallow/wake-ok counters, ASPM parity |
| PM gate helper | mirrors `hcc_core_pm_request_sleep` exactly | 73/73 host checks prove all four gate outcomes before any fire |
| ASPM probe read | read-only | record LNKCTL at probe; warn if ASPM is unexpectedly enabled; no device write at probe |
| `aspm_disable` WO attr | clears only if bits are set | disabling ASPM cannot break a trained link; enabling is not offered |
| `hcc_poll_idle_ms` | 20 | after 25 quiet passes in ACTIVE the thread sleeps 20 ms; any decoded work snaps back to the active 5 ms cadence immediately; pre-ACTIVE boot keeps the fixed fast cadence |
| `data_tx_poll_ms` | 5 | clamp 1..100; the completion timeout (200 ms) still bounds the lane; 5 = shipped behaviour |

Build identity of the patched module (host cross-build, GPL-2.0 source, local
artifact only):
- base: FINAL association module, 56 params (gate in
  `tools/build-patched-module.sh`);
- patched: **60 params**, stripped **349,892 B**,
  sha256 `0143c6370695925d479f07d5511231521503130d088c1c41af46c90ca65a5b9e`
  (`evidence/build.txt`); build-id is the only run-to-run variable;
- 0 warnings / 0 errors / no undefined symbols.  **Not fired, not in any
  image.**

## 5. Performance analysis

The instrument is the independent model
[tools/perf_model.py](tools/perf_model.py); its output is committed as
[evidence/PERF-MODEL.md](evidence/PERF-MODEL.md).  The host selftest checks
the same formulas from the driver's own header, so the two implementations
must agree.

### 5.1 TX path structure and per-frame host cost

Host->AP data TX (source: `hi5622_data_tx.c`, `hi5622_dp_txrx.c`):
build the 802.11 MPDU in a stack buffer (24 B header + LLC/SNAP 8 B + payload,
**<= 768 B**), copy one 8 B MSDU-ring entry, `dma_wmb`, publish the packed
index into the device-RAM MSDU-info table **with a read-back**, publish the
TXQ-table queue state, and rearm a 5 ms completion delayed work that reads
the device rptr and frees skbs.  There is **no per-frame mailbox/H2D
message** (the MAC DMA walks the host ring), so the per-frame host cost is a
handful of MMIO reads/writes, not a protocol round trip.

### 5.2 Ceilings: host window vs non-aggregated airtime

Two independent ceilings apply.  The host window is bounded by the in-flight
pool and the completion poll; the air ceiling is set by the lack of A-MPDU.

| pool | poll | host frames/s | MPDU MB/s | 736 B payload Mbps |
| ---: | ---: | ---: | ---: | ---: |
| **16** | **5 ms** | **3200** | **2.46** | **18.8** |
| 32 | 5 ms | 6400 | 4.92 | 37.7 |
| 64 (ring depth) | 5 ms | 12800 | 9.83 | 75.4 |
| 16 | 1 ms | 16000 | 12.29 | 94.2 |

| MPDU | PHY (2.4 GHz) | frame us | cycle us | frames/s | 736 B payload Mbps |
| ---: | --- | ---: | ---: | ---: | ---: |
| 768 | 65 Mbps MCS7 1SS | 135 | 291 | 3436 | 20.2 |
| 768 | 130 Mbps MCS15 2SS [I] | 88 | 244 | 4098 | 24.1 |
| 1500 | 65 Mbps MCS7 1SS | 225 | 381 | 2624 | 15.4 |

**Conclusion:** at the shipped defaults and 2.4 GHz MCS7 the host window
(3200 fps) is *below* the air ceiling (3436 fps) by 6 % - the stack is
**air-bound**.  Cutting the completion poll to 1 ms without also raising the
pool (to at most the 64-entry ring depth) buys nothing on air; raising the
pool is a memory/bookkeeping change and a bench ladder (P3), not a free win.
The only step that removes the per-frame overhead is A-MPDU/A-MSDU
aggregation, which this clean-room stack does not implement (firmware BA
session support unproven); the vendor comparison is therefore qualitative,
not a numeric promise.

### 5.3 Scan pace

Measured [V*]: ~**156 ms/channel**, then a hard stop at **6 channels per
g0 id0xC request** (firmware-side; the host cannot fix it by waiting longer).
The driver already splits a cfg80211 request into `scan_req_batch` (default
6) requests with a 300 ms gap:
- 6 channels, one batch: 936 ms;
- 13 channels as 3 x 6/6/1 batches: 2028 + 600 = ~2.6 s;
- full 13 in one request: 2028 ms *if* the firmware processed them all - it
  does not, so the batched form is the only honest shape.
The H1 band-config rung (7-8 channels/request) is the documented ceiling; a
complete 13-channel scan is structurally blocked at the DMAC per the sibling
analysis.  Sweep levers that already exist: `scan_dwell_ms` (vendor default
60, allowed 20..200), `scan_req_batch`, `scan_batch_gap_ms`.

### 5.4 H2D round trip (measured, not a bottleneck for data TX)

Baseline [V*]: announce consumed in **0 polls** in all 57 captures; the
8.29 ms write->consumed and 42.8 ms write->completion medians are dominated
by serial-console printk and the protocol tick, not wire latency.  Data TX
does not use H2D per frame, so this does not cap throughput; it matters for
control sequences (AP start, scan, station ops), where the 0-poll consumption
means the bench's 100 ms frame-gap policy, not the protocol, sets the pace.

### 5.5 Beacon / DTIM

The AP beacons every 100 TU (102 ms) with DTIM 2 (vendor AP facts [V*]).
Because our chip is always-on, DTIM changes **client** power only, not AP
power: client wakeups fall from 294/min (DTIM 2) to 118/min (DTIM 5) to
59/min (DTIM 10).  This is a client-experience trade-off (multicast latency)
for after association is proven; it is not an AP power feature until chip
power gating exists.

### 5.6 CPU idle / poll cost

The service thread polls every 5 ms per endpoint = **200 wakeups/s/endpoint**
(measured ~145 effective/s in the live sample).  The safe back-off drops
this to 50/s after 125 ms of quiet in ACTIVE.  There is no deep idle path to
protect on this SoC in our stack (no runtime PM hooks), so the gain is
"wakeups avoided", measurable with `/proc/stat` before/after; the ladder P2
pins it.  Do not combine a longer `hcc_poll_idle_ms` with a busy AP without
checking EAPOL/data RX latency - the fast cadence is restored by the first
decoded frame, so only the first packet after an idle gap can be late.

## 6. Recommendations (ranked)

1. **Ship the safe-subset patch defaults as-is** (PM off, ASPM assert,
   idle back-off 20 ms).  No bench dependency; the only visible change is
   fewer idle wakeups.
2. **Ladder P1/P2 first** (instrumented baseline + idle cadence A/B): cheap,
   reversible, no device semantics touched.
3. **Do not chase TX throughput below 2.4 GHz MCS7 without aggregation.**
   The host window is already within 6 % of the air ceiling.  P3 (pool/poll
   sweep) only pays on a faster PHY (5 GHz / 2SS) and is a memory change.
4. **Keep scan batching at 6** and treat 7/8-channel H1 as an experiment;
   a full 13-channel scan is DMAC-blocked, not host-tunable.
5. **Post-association only**: DTIM/beacon client-power experiments (P6) and
   the STA power-save path (cfg80211 M6) belong after the association chain
   is green; neither is needed for the current AP proof.
6. **P5 (PM sleep) only as the last attended rung** with physical
   power-cycle recovery armed: it is the one change that can hang the chip
   service with no host-side reset.

## 7. Limits and unknowns

- The vendor PM dispatch table could not be followed by symbol (section 2.2
  caveat): the boot-time caller chain of `hmac_config_pwr_on` is [I].
- The exact vendor PM timer period (100 ms-ish from the `hcc_timer_register`
  arguments) is [I]; it does not matter because the vote is dead.
- The frame-timing inputs of the air model are classic 802.11 values [I]; the
  firmware's rate selection for data frames is unproven, so the 18.8 vs 20.2
  Mbps comparison is a model, not a measurement.  The ladder P3 is what turns
  it into [V*].
- The vendor's per-VAP power gating was not re-proven end to end (it needs a
  stock boot with instrumentation); it is irrelevant to our always-on stack.

## 8. Evidence and reproduction

| evidence | file / command | grade |
| --- | --- | --- |
| portable logic selftest, 73 checks | `tools/pm_perf_selftest.c`, output `evidence/selftest.txt` | host PASS |
| independent performance model | `tools/perf_model.py --write evidence/PERF-MODEL.md --check` | host PASS |
| patch applies to the pinned base | `tools/run-host-checks.sh` step 3 | host PASS |
| patched module cross-build, 0 warnings, 60 params, 349,892 B | `tools/build-patched-module.sh`, output `evidence/build.txt` | host PASS |
| vendor PM functions (sec 2) | `tools/vendor_pm_disasm.py --ko <local vendor .ko> <symbols...>` (local evidence; output never committed) | [V] |
| bench timings quoted as [V*] | `wifi-perf-baseline` baseline + ledger 2026-11-27 20:30 | [V*] |

Reproduce everything device-free:

```sh
cd opensource/docs/incoming/wifi-power-perf
./tools/run-host-checks.sh --with-build
# vendor RE (needs the operator-local vendor modules; never committed):
python3 tools/vendor_pm_disasm.py --ko /home/.../hi5622v100_plat.ko \
    wlan_pm_init wlan_pm_try_to_sleep wlan_pm_sleep_work wlan_pm_wakeup_work \
    plat_pm_wakeup wlan_power_on wlan_power_off low_power_init plat_pm_vote
```
