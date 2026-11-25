# REPORT - the decisive MAC-RX capture experiment (E-RX-1)

**Lane:** wifi-rx-capture-plan (shared task-165). **Host-only design lane.**
The Lead fires; this lane never contacts the bench router.
**Written:** 2026-11-25 (against the d2c608e7 association module: rx-static live
bank + rx-refill watermark + corrected HCC g0 id6 mgmt carrier).
**Deliverables:** this report, [RUNBOOK.md](RUNBOOK.md), [ANALYSIS-TEMPLATE.md](ANALYSIS-TEMPLATE.md),
the READ-ONLY router instrument [scripts/router/rx-capture-router.sh](scripts/router/rx-capture-router.sh),
the host orchestrator [scripts/host/rx-capture-host.sh](scripts/host/rx-capture-host.sh) and the
classifier [scripts/host/analyze_capture.py](scripts/host/analyze_capture.py).

Evidence markers: **[V]** decoded from the named local vendor artifact (never
committed); **[V*]** measured by an earlier attended bench lane and quoted from
the committed reports; **[L]** live bench state quoted from the task brief;
**[I]** inference; **[U]** unknown.

---

## 1. TL;DR

The one open bug is: the AP is up (hostapd AP-ENABLED), the client provably
transmits 802.11 authentication frames at our BSSID, yet the router's MAC RX
path produces nothing: no D2H management-RX event (g1 id13), the live host-MAC
bank never moves (CA 0x40050000: status 0x182, norm/small rptr 0x20, cmp wptr
0), and hostapd sees nothing **[L]**. The RF path is otherwise proven (beacons
are received by clients, the client's TX works elsewhere, -33 dBm link) **[L]**.

E-RX-1 is designed to distinguish four mechanically different failures with one
attended, **read-only** capture:

| hypothesis | what must be true | decisive observation |
| --- | --- | --- |
| **H1 - MAC/PHY never received** | nothing reaches the host-visible RX path | status word gains no RX bit, no completion write, no buffer write, no D2H delta, no exception |
| **H2 - MAC saw it but could not write it** | the MAC-side RX machinery ran and failed | status gains bit 5 (data-ring overrun) and/or bit 10 (rx_ppdu_desc error); maybe a partial buffer write; firmware exception possible |
| **H3 - firmware received it but did not deliver the event** | device-internal RX happened, no host event | D2H catalogue gains no (g1,13); status may be clean; host-side delivery lines absent |
| **H4 - the experiment is contaminated by the firmware exception** | the page/mask/staging changes mid-run | crash-first.txt / sampler deviation |

Two design decisions make the experiment decisive and safe:

1. **No drain.** E-RX-1 never calls dp_rx_drain, never moves a ring pointer and
   never writes +0x44/+0x48. The known firmware-exception class (mask
   0x04000010) appeared after extended drain loops **[L]**, and the RX side
   currently writes only through the drain; removing all drain writes isolates
   the RX question from the crash question. The router script performs **no
   device writes at all** (its only optional sysfs stores are the documented
   read-only one-shot dp_rx_arm/dp_rx_poll triggers, default off).
2. **The host-MAC interrupt status word is sticky on our bench.** Our driver
   never enables or services the MAC interrupt, so status bits latch in
   CA 0x40050000 +0x00 until a W1C; the vendor ISR that would clear them is not
   running. A single pre/post read of that word therefore records every RX
   interrupt source that fired during the trigger window - including the
   error/overflow classes that discriminate H2.

The capture also decodes, for the first time as a bench read, the **full vendor
host-MAC interrupt status bit map** (section 5.2): TX completion, RX completion,
norm/small free-ring-empty, data-ring overrun, location-complete, BA-info ring
overrun, BA-win exceeded and rx_ppdu_desc error. These bit names come from the
local vendor module ISR and its log strings [V]; only derived bit numbers and
names are recorded here, no vendor bytes.

A zero data-ring delta is an **expected null** for management-only traffic
(the firmware delivers management frames as CRX/D2H events, not through the
norm/small MSDU rings - section 3 and the sibling-GPL split). E-RX-1 therefore
uses the ring-memory sweep as a falsification test for an unexpected data-path
write, not as the primary management datum.

---

## 2. Evidence base (what is already proven)

| fact | evidence |
| --- | --- |
| AP starts with the vendor 5-message order; hostapd AP-ENABLED; WPA2 GTK installed; mgmt TX on the corrected HCC g0 id6 carrier accepted by firmware (consumed 0 polls) | wifi-ap-start, wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md T1, wifi-stability/FINAL-INTEGRATION.md; task brief [L] |
| MAC data-plane TX works: TXQ-table address sync + TX COMPLETED (TXQ publish, rptr==wptr) | wifi-dp-tuning/REPORT.md, wifi-re-report/REPORT.md [V*] |
| RX free-ring consumer is armed: live bank CA 0x40050000 status 0x182, mask 0xfffffcc1, norm/small rptr 0x20 (= vendor stock mirror, i.e. the MAC consumed 32 published free entries) | wifi-rx-static/REPORT.md, arm fire [V*]; task brief [L] |
| The live bank is the right bank: CA 0x40050000 (+0x00 status, +0x08 norm rptr, +0x0c small rptr, +0x10 cmp wptr); CA 0x40040000 +0x4c/+0x50/+0x54 are not live counters (vendor never reads them) | wifi-rx-static/REPORT.md sections 2-3 [V]/[V*] |
| Completion entry = device VA of buffer+4; 36-byte device header at buffer+4; payload length at header+0x1a; next at header+0x20; MPDU at header+0x24 (buffer+0x28) | wifi-rx-static/REPORT.md section 5 [V]; wifi-rx-watermark/REPORT.md [V] |
| Management RX arrives as D2H HCC group 1 id 13 (60-byte prefix = 12-byte HCC + 48-byte RX info; channel at +7, u16 frame length at +17, s8 RSSI at +39, frame at +48); ring-entry processor never dispatches management frames | wifi-ap-mgmt/TABLES.md T5, wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md T3, wifi-re-report/REPORT.md 6.21 [V] |
| No luofu SET_RX_FILTER message exists; publishing the free-ring producer is the whole host-side RX enable (sibling Hi3881/Hi1105 + our vendor module) | wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md T2 (evidence-backed negative), wifi-sibling-gpl/REPORT.md [V] |
| The firmware exception class: page 0x86807fef/7/2/0xabcd loses +8==2/golden, vector-seen mask CA 0x400008 becomes nonzero (0x04000010 observed), hcc_events excp_announce=1, vector_mask 0x10; 17-word context frame at CA 0x40000c..0x400044 | wifi-host-replica/REPORT.md 6.4, wifi-fw-ready-gate/REPORT.md 10/12, wifi-stability/HARDENING.md; task brief [L] |
| Client auth frames are on air: client kernel log "send auth to 06:05:04:03:02:01 ... try 1/3..3/3, authentication timed out"; client harness at wifi-assoc-e2e | task brief [L]; wifi-assoc-e2e/REPORT.md |
| Firmware RAM window: BAR0 = 0x6b8000 + CPU address (region 5); IO region 3: BAR0 = 0x3b8000 + (CA - 0x40000000) | hi5622_contract.h, hi5622_pciet.h (committed driver sources) [V] |
| Device DMA addresses are CPU physical addresses: DTS memory@80500000 + identity dma-ranges 0x80000000..0x8fffffff, kernel DEVMEM=y with STRICT_DEVMEM unset | committed DTS + image kernel config [V] |


---

## 3. Instrument model - what each channel can and cannot prove

| channel | exact source | can prove | cannot prove |
| --- | --- | --- | --- |
| live host-MAC status (CA 0x40050000 +0x00) | raw BAR0 read (sticky, section 5.2) | that a host-MAC RX interrupt source fired (RX completion, overrun, PPDU-desc error) and when | whether the PHY demodulated a specific frame; the frame identity |
| live ring pointers (+0x08/+0x0c/+0x10) and config producers (+0x38/+0x3c/+0x40) | raw BAR0 read | free-ring consumption, completion producer movement, host consumer state | anything about management-frame delivery |
| RX ring memory (norm 256x4, small 512x4, cmp 4095x4) | raw /dev/mem at the ring device VA (identity map) | whether the MAC wrote a completion entry or a host-visible MSDU; a non-zero completion ring is a hard data-path fact | management-frame reception (management does not use these rings) |
| per-buffer 36-byte device headers | raw /dev/mem at devVA(buffer+4) for every published buffer | whether the MAC wrote ANY bytes into a host buffer (partial/failed write included) | a frame that was dropped before the write |
| D2H catalogue (rd_catalog / rd_decode / hcc_events) | driver read-only sysfs | whether the firmware emitted any device->host event family, especially g1 id13 management RX | firmware-internal handling without an event |
| firmware exception state (page/state/mask/staging/context) | driver boot attr + raw FW-RAM reads | that the firmware trapped and where; page corruption | the cause of the exception |
| dmesg / hostapd log / cfg80211_status | read-only files | host-side delivery and AP-layer progress | device-side causes |
| client-side dmesg auth lines | host | that the trigger frames left the client | that they were on the right channel/RF (use phase 0b) |

**The management/data split matters.** The MAC norm/small rings carry
device-written MSDU frames; management frames are delivered by the firmware as
CRX/D2H events **[V]**. During a management-only trigger (auth/probe) a
*correct* RX path can still leave the completion ring at zero. E-RX-1 therefore
first decides on the D2H catalogue and the sticky status word, and treats the
ring memory as an additional falsification channel (section 7).

---

## 4. Address map and exact reads

### 4.1 Translation rules (from the committed driver contract)

| window | rule | used for |
| --- | --- | --- |
| IO region 3 | BAR0 = 0x3b8000 + (CA - 0x40000000), CA 0x40000000..0x4011ffff | MAC band-0 live bank and config block |
| FW-RAM region 5 | BAR0 = 0x6b8000 + firmware CPU address | handshake page, state, exception mask/context |
| device host-DRAM | device VA == CPU physical (DTS memory@80500000, identity dma-ranges 0x80000000..0x8fffffff) | RX ring memory and RX buffer headers through /dev/mem (read-only) |

BAR0 physical base = first line of /sys/bus/pci/devices/<dev>/resource; the
router instrument reads it at run time, so no host address is hard-coded.
Raw access is read-only: /dev/mem is opened with dd in read mode only; the
script contains no write form and its host-side selftest proves it (13 checks).

### 4.2 Read matrix (what the router script captures)

| # | read | address / source | cadence | purpose |
| --- | --- | --- | --- | --- |
| R1 | live bank status, norm rptr, small rptr, cmp wptr | BAR0 + 0x408000, 5 words | every sampler tick (default 5 Hz) | primary RX activity/error signal |
| R2 | whole live window 0x40050000..+0x3ff | BAR0 + 0x408000, 256 words | snapshot pre/post | discover counters the static decode missed; diff |
| R3 | config block +0x38/+0x3c/+0x40/+0x44/+0x48, whole window | BAR0 + 0x3f8000, 256 words | snapshot pre/post | producers/consumer, W1C status, mask, ring addresses/sizes |
| R4 | page 0x1bf000 (8 words), state 0x118b60 | BAR0 + 0x877000 / 0x7d0b60 | every tick | boot/golden + boot-task state |
| R5 | vector mask 0x400008, staging 0x40000c, 17-word context 0x40000c..0x400044 | BAR0 + 0x6f8008 / 0x6f800c | every tick (mask/staging), snapshot (frame) | crash detection + first-fault context |
| R6 | norm ring 256x4 B, small ring 512x4 B, cmp ring 4095x4 B | /dev/mem at the device VAs printed by dp_status "host rings" line | snapshot pre/post | completion writes, free-ring state |
| R7 | 36-byte header + 28 payload bytes of every published norm/small buffer | /dev/mem at each ring entry (entry = devVA(buffer+4)) | snapshot pre/post | did the MAC write ANY bytes into a host buffer |
| R8 | boot, hcc_status, hcc_events, hcc_ready, hcc_rings, dp_status, dp_rings, dp_tx_status, dp_rx_capture, dp_mac_ext, cfg80211_status, data_tx_status, rd_status, rd_catalog, rd_decode, module params | driver sysfs (read-only) | snapshot pre/post; hcc_events every tick | D2H catalogue, gates, module identity |
| R9 | dmesg, wireless state (iw dev/info/station dump), process list | router files | snapshot pre/post | delivery lines, AP state, drain loop presence |
| R10 | client auth evidence | host harness output + client dmesg | per attempt | proof the trigger transmitted |

Expected preconditions (record them; a mismatch is a finding, not noise):
live status **0x182**, norm/small rptr **0x20**, cmp wptr **0**, mask
**0xfffffcc1**, page **0x86807fef/7/2/0xabcd** (golden), state **1**, exception
mask **0**, staging **0**, hcc_events excp_announce **0**, module param
rx_enable=1, and the dmesg "DP rx_arm: live bank" line from this load.
If dp_rx_arm was never run in this load, the live-bank precondition is not
valid: stop and run the documented AP-first order (or use the one-shot
--allow-oneshot fallback only if /dev/mem is unavailable).

### 4.3 Client trigger protocol

The client harness from [../wifi-assoc-e2e](../wifi-assoc-e2e/REPORT.md)
already produces the proven auth frames. E-RX-1 runs 4 attempts inside a 90 s
capture window:

1. host reset + scan on the AP channel (probe requests; also re-confirms the
   AP is visible),
2. one wpa_supplicant auth attempt per rung (open-system auth is sent even for
   a WPA2 AP; the harness classifies AUTH-TIMEOUT as expected),
3. the client dmesg "send auth ... try 1/3..3/3" lines are saved as the proof
   of transmission,
4. attempts are spaced ~2 s so the sampler sees a clean before/during/after
   pattern,
5. the router sampler runs before, during and 5 s after the last attempt.

The router sampler timestamps every tick with epoch time; the host marker file
records the start/end epoch of every attempt, so a crash or a status change can
be attributed to a time window even if the two clocks are offset (the capture
window is bounded by the pre/post snapshots).

Optional host-only phase 0b (no router login) strengthens the classification:
create a monitor interface on the client, tune it to the AP channel, capture
while one auth attempt runs, and check whether the AP transmits an 802.11 ACK
or any response frame after the client's auth frame. An ACK proves the router's
PHY/MAC received the frame (then H1 is excluded at the radio level and the bug
is firmware/event delivery); no ACK leaves the RX chain/mode suspect. MT7922
monitor+managed combinations are not guaranteed - treat a failure to create the
monitor interface as unavailable, not as evidence.


---

## 5. Register and event decode tables

### 5.1 Live bank CA 0x40050000 (host-MAC word 8) [V]/[V*]

| offset | name | observed healthy value | meaning |
| --- | --- | --- | --- |
| +0x00 | host-MAC interrupt status | 0x00000182 | sticky interrupt/error status (decode in 5.2) |
| +0x04 | (unused in the decode) | 0 | captured raw only |
| +0x08 | norm free-ring device consumer | 0x00000020 | the MAC consumed 32 published norm entries |
| +0x0c | small free-ring device consumer | 0x00000020 | same for the small ring |
| +0x10 | completion-ring device producer | 0x00000000 | **must move if the MAC writes any completion** |

The vendor's host driver binds these words via its machw object (word 8); the
config block CA 0x40040000 +0x4c/+0x50/+0x54 are NOT live counters and are
never read by the vendor (the stock dump keeps them 0) [V].

### 5.2 Host-MAC interrupt status bit map (new decode; derived from the vendor ISR) [V]

Source: the local vendor wifi module ISR (function disassembly + the log
format strings it selects) and the vendor mask constant 0xfffffcc1 in the chip
IRQ init [V]. Only bit numbers and derived names are recorded; no vendor bytes.

| bit | name | class | notes |
| --- | --- | --- | --- |
| 0 | tx_complete_alias | TX | same handler as bit 1 |
| 1 | tx_complete | TX | host TX completion; set in the observed 0x182 |
| 2 | **RX_complete** | RX | completion ring has entries to drain; the normal RX branch |
| 3 | norm_ring_empty | RX | norm free ring needs refill |
| 4 | small_ring_empty | RX | small free ring needs refill |
| 5 | **data_ring_overrun** | RX error | completion ring overrun; vendor logs read idx (config +0x40) and write idx (live +0x10) |
| 6 | location_complete_error | error | location/CSI completion error |
| 7 | ba_info_ring_overrun | warning | BA-info ring overrun |
| 8 | ba_info_ring_overrun | warning | second BA ring (same log string) |
| 9 | ba_win_exceed_error | error | BA window exceeded |
| 10 | **rx_ppdu_desc_error** | RX error | PPDU-descriptor / completion-write error; vendor logs read idx (config +0x40) and write idx (live +0x10) |
| >10 | unknown_gt10 | error | default vendor error log |

Mask semantics: the vendor init writes +0x48 = 0xfffffcc1, i.e. only bits
**1,2,3,4,5** are unmasked; a mask bit 1 means masked. Masked bits still latch
in the raw status word: the observed healthy word **0x182 = bit 1 + bit 7 +
bit 8** already carries two *masked* sources (7 and 8), which proves the latch
behaviour empirically. The capture therefore reads the whole word. When one of
the error bits fires, the vendor ISR unmasks the related bits and forwards an
exception event to the firmware; that ISR is not running on our bench, so the
status simply accumulates - which is exactly what makes the read decisive.
The 0x182 bits are baseline, not trigger evidence; the classifier compares
pre, during and post. The two primary H2 signals (bit 2 RX_complete, bit 5
data_ring_overrun) are unmasked by default and therefore unambiguous.

### 5.3 Config block CA 0x40040000 (host-interface word 5) [V]

| offset | name | value on the bench module | decode |
| --- | --- | --- | --- |
| +0x00 | tx_ba_info depth | 0x100 | 256 entries |
| +0x04 | tx_ba_info address | device VA | host-DRAM |
| +0x10 | MSDU-info ring-pointer table | ACP alias | from g4 id2 payload |
| +0x1c | RX buffer lengths | 0x00b0063c | norm 0x63c / small 0xb0 |
| +0x20/+0x24/+0x28 | norm/small/completion ring addresses | device VAs | cross-check vs dp_status |
| +0x2c/+0x30/+0x34 | ring depths | 256/512/4095 | bits 11:0 |
| +0x38/+0x3c | norm/small free-ring producers | 0x00008000 | index 0, phase 1 = full ring published |
| +0x40 | completion-ring host consumer | 0x00000000 | advanced only by a drain (not used in E-RX-1) |
| +0x44 | interrupt W1C status | 0 | written only by the vendor ISR / drain with rx_clear_status |
| +0x48 | interrupt mask | 0xfffffcc1 | see 5.2 |
| +0x4c/+0x50/+0x54 | dead config mirrors | 0 | **not** live pointers |

### 5.4 Completion entry and per-buffer header [V]

* free-ring entries and completion-ring entries are 4-byte little-endian device
  VAs; for a RX buffer the value is **devVA(buffer + 4)**.
* the device writes a 36-byte header at **buffer+4**: payload length u16 at
  header+0x1a (= buffer+0x1e), next sub-MSDU devVA u32 at header+0x20
  (= buffer+0x24), flags at header+0x05, status nibble at header+0x07; the
  MPDU starts at header+0x24 (= buffer+0x28).
* E-RX-1 reads 64 bytes at each entry value: header + first 28 payload bytes.
  The pre/post sweep is a byte-level diff; **any** change from the zeroed
  baseline is a MAC write, even if the frame is partial or the header is
  malformed.

### 5.5 Management RX event g1 id13 [V]

Wire shape: 12-byte HCC header + 48-byte RX info + 802.11 frame; total length =
60 + frame length. RX info fields: channel at +7, u16 frame length at +17, s8
RSSI at +39. The frame is delivered to cfg80211 only when the driver's mgmt_rx
path is complete. On the D2H direct reader, the raw event appears in
rd_decode as a line with g=1 id=13 and the payload hex (first bytes are the
RX-info block; the frame starts at payload offset 48), and its counter appears
in rd_catalog as g=1 id=0x0d. Absence of both is the primary H1/H2/H3
discriminator.

### 5.6 Firmware exception state [V]

| object | address | decode |
| --- | --- | --- |
| handshake page | CA 0x1bf000 (BAR0 + 0x877000) | golden 0x86807fef / 7 / 2 / 0xabcd; page+8==2 = CPU1 ack; golden lost = firmware RAM overwritten/corrupted |
| boot task state | CA 0x118b60 | 1 = boot task ran |
| vector-seen mask | CA 0x400008 (BAR0 + 0x6f8008) | halfword bits 1..4 = which vector entries ran; 0 = no exception |
| staging / context frame | CA 0x40000c..0x400044 (BAR0 + 0x6f800c) | 17 words {r0-r12, sp, lr, lr_exc, spsr} of the trapping context; first word observed as staging=0 in a healthy boot |
| exception announce | hcc_events excp_announce / vector_mask | out[1] bit 2 = panic/vector announce; the observed crash class is mask 0x04000010, vector_mask 0x10, excp_announce=1 |


---

## 6. Issue B (the firmware exception): candidate triggers and the isolation ladder

### 6.1 What our RX code writes, compared with the vendor

| write | vendor | our driver | risk |
| --- | --- | --- | --- |
| config ring addresses/sizes/buffer lengths (+0x20/+0x24/+0x28/+0x2c/+0x30/+0x34/+0x1c) | init once | dp_setup, read-back verified | same values as vendor; low |
| free-ring producers (+0x38/+0x3c) | at init and after refill, packed index+phase | dp_setup (0x8000) then every drain/refill | **risk if a refill publishes a producer the device has not consumed** (stale window/phase) or double-publishes a buffer |
| completion consumer (+0x40) | advanced after drain | only in dp_rx_drain | **risk if a wrong packed index/phase is published ahead of the producer** |
| W1C status (+0x44) | vendor ISR clears served bits | only when rx_clear_status=1 | clearing a firmware-owned status could perturb the firmware |
| interrupt mask (+0x48) | RMW on error/unmask | dp_setup initial value only | low |

The crash appeared after extended drain loops in the live session **[L]** and
the previous 0x04000010 class was produced by a user_del + user_add/notify
re-create **[V*]**. Two mechanisms are plausible and both are testable without
touching the RX experiment:

1. **Pointer race / wrong packed index.** The drain reads the live consumer
   rptr, then publishes a producer after computing a delta. If the device
   consumes more entries in between, or if the phase bit is mishandled at the
   wrap, the published producer can describe a ring state the device cannot
   reconcile; the MAC then faults while writing a descriptor (rx_ppdu_desc
   error / data-ring overrun) and the firmware traps.
2. **Buffer ownership.** A returned buffer is re-published while the firmware
   still owns it (or a stale entry is reused); the next receive overwrites
   live memory outside the intended ring slot.

Neither is proven. The isolation ladder (each rung needs a fresh power-on; the
Wi-Fi chip is not reset by a SoC reboot):

| rung | module state | drain actions | expected |
| --- | --- | --- | --- |
| B0 | arm, no drain, no client | none, idle 10 min | no exception (baseline) |
| B1 | arm, no drain, client trigger | none | E-RX-1 (this experiment) |
| B2 | arm, rx_refill_wm=0, no client | ONE drain call | no exception expected; if exception -> the single publish is the trigger |
| B3 | arm, rx_refill_wm=0, no client | drain loop 60 s at 1 Hz | isolate the old (non-watermark) drain path |
| B4 | arm, rx_refill_wm=75, no client | drain loop 60 s at 1 Hz | isolate the watermark pass |
| B5 | arm, rx_clear_status=1, no client | ONE drain call | isolate the +0x44 W1C |

Every rung uses the router crash watch and stores crash-first.txt; stop at the
first exception and power-cycle. The capture script supports B0/B1 directly
(sampler + client trigger or skip-client); B2-B5 need the module parameters from
the fire recipe and are described here as design, to be scheduled after E-RX-1.

Fail-closed recommendations for the patch lane (not implemented in this lane):
read back the live consumer immediately before and after every producer publish;
publish only a phase-correct packed index; never re-publish a buffer unless it
was returned by a validated completion entry in this action; bound each action;
keep +0x44 behind rx_clear_status and off by default; treat any live-bank read
of 0xffffffff or any consumer/producer distance outside [0,depth] as a STOP
without a write.

---

## 7. Analysis criteria and decision tree

### 7.1 Signals

| id | signal | source |
| --- | --- | --- |
| S1 | live status word delta vs baseline / any RX bit (2,5,10) | sampler + pre/post |
| S2 | completion producer +0x10 moved / any non-zero completion entry | sampler + ring memory |
| S3 | any per-buffer header byte changed vs pre | R7 sweep |
| S4 | D2H catalogue delta, especially g1 id13 count > 0 or a decoded g1 id13 frame | rd_catalog / rd_decode |
| S5 | host delivery lines (mgmt RX delivered / cfg80211) | dmesg |
| S6 | firmware exception evidence (page/mask/staging/excp_announce) | sampler + R4/R5 + crash.txt |
| S7 | client transmission proof (markers, "send auth" lines) | host bundle |
| S8 | trigger window alignment (epoch markers vs sampler timestamps) | sampler + host markers |

### 7.2 Verdicts (implemented by the classifier)

| verdict | condition | next action |
| --- | --- | --- |
| **V-NOTRIGGER** | S7 absent | re-run the client trigger; no router conclusion |
| **V-CRASH** | S6 present (any rung) | power-cycle, run the B isolation ladder; RX capture invalid |
| **V-EVENT-NOT-DELIVERED** | S4 present, S5 absent | audit the g1 id13 decoder/delivery path (60-B prefix decode, cfg80211 delivery) |
| **V-DELIVERED-NO-AP** | S4 present, S5 present | AP FSM/hostapd gap; re-run hostapd -d |
| **V-MAC-WROTE-NOEVENT** | S2 or S3 positive, S4 absent | data path works; management event path is the gap (firmware VAP/user state) |
| **V-MAC-SAW-NOWRITE** | S1 has RX error bits (5/6/10) or bit 2 without S2/S3 | audit ring config/DMA/buffer ownership vs vendor (section 6.1) |
| **V-MAC-NOSEE** | S1-S6 all negative, S7/S8 positive | RF/PHY/MAC-enable path (phase 0b ACK/probe-response discriminator); do not touch the decoder |
| **V-INCONCLUSIVE** | raw and/or sampler channel missing | restore the missing channel and re-run |

### 7.3 What must NOT be concluded

* **"The completion ring is zero, therefore the MAC is blind."** Management
  frames do not use the MSDU rings; a zero ring is the expected null for a
  management-only trigger. The verdict rests on S1+S4.
* **"g1 id13 is absent, therefore RF is dead."** The firmware may drop a
  management frame before generating the event (VAP/user state); s1 error bits
  or the phase 0b ACK result separate radio reception from firmware handling.
* **"No mgmt RX event means a missing RX filter message."** The luofu
  SET_RX_FILTER message is an evidence-backed negative (sibling-GPL T2); a
  missing event is not fixed by inventing one.
* **"The last dmesg line is the cause."** The crash class appeared after drain
  loops; attribute the first deviation to a time window, then isolate with B2-B5.
* **"Page golden == everything is fine."** The page can be restored/re-written
  by the firmware while other state is lost; compare the whole R4/R5 set.

### 7.4 Evidence acceptance

The classifier refuses to guess: it only produces a verdict from observations
actually present in the bundle, records every channel value in its evidence
table, and emits V-INCONCLUSIVE when a required channel is missing. Synthetic
fixtures for all eight verdicts run in the host selftest (8/8 PASS, see
evidence/analysis-selftest.txt).


---

## 8. Deliverables and how to run them

| artifact | what it is |
| --- | --- |
| [RUNBOOK.md](RUNBOOK.md) | the attended E-RX-1 procedure: roles, preconditions, exact commands, timing, stop rules, evidence naming |
| [ANALYSIS-TEMPLATE.md](ANALYSIS-TEMPLATE.md) | the fill-in tables and the verdict worksheet the Lead completes from the bundle |
| [scripts/router/rx-capture-router.sh](scripts/router/rx-capture-router.sh) | READ-ONLY router instrument: check / snapshot / run sampler / stop / status / collect / selftest |
| [scripts/host/rx-capture-host.sh](scripts/host/rx-capture-host.sh) | host orchestrator: preflight, push, check, pre snapshot, sampler, client trigger, post snapshot, collect/pull, analyze |
| [scripts/host/analyze_capture.py](scripts/host/analyze_capture.py) | bundle classifier (eight verdicts) with a synthetic-fixture selftest |

Host-side verification (no device):

    cd opensource/docs/incoming/wifi-rx-capture-plan
    sh scripts/router/rx-capture-router.sh selftest          # 13 structural checks, PASS
    python3 scripts/host/analyze_capture.py selftest         # 8/8 verdict fixtures, PASS
    sh scripts/host/rx-capture-host.sh selftest              # script presence + analyzer selftest

Attended run (the Lead, on the bench workstation):

    opensource/docs/incoming/wifi-rx-capture-plan/scripts/host/rx-capture-host.sh preflight
    opensource/docs/incoming/wifi-rx-capture-plan/scripts/host/rx-capture-host.sh run \
        --label rx1-exp --attempts 4 --seconds 90 --hz 5

The run writes host evidence under build/wifi-rx-capture/<label>/ (gitignored),
pulls the router bundle and prints the classifier verdict. A crash on the router
is exit code 3 and a V-CRASH classification.

## 9. Honest limits

* The router instrument needs readable /dev/mem for the live-bank sampler; the
  bench kernel config has DEVMEM=y and STRICT_DEVMEM unset, but the devmem
  userspace tool is optional (the script uses dd + od/hexdump). If /dev/mem is
  unavailable, the fallback is one-shot dp_rx_arm/dp_rx_poll dmesg lines, which
  cannot sample during the trigger: the classifier will report V-INCONCLUSIVE
  for the live-bank channel.
* The capture cannot see firmware-internal state. "Firmware received it but
  did not emit an event" is inferred from the absence of every host-visible
  signal, not observed directly; the phase 0b ACK/probe-response observer is the
  only independent radio-level check available without firmware instrumentation.
* Management-only traffic is an expected null for the data rings. A data-plane
  RX test needs an associated station (EAPOL/data after a successful auth),
  which is itself gated on the management RX bug - hence the ordering.
* The sampler cadence (default 200 ms) can miss a transient status bit that the
  firmware clears by itself; the status word is sticky on our bench (no host
  ISR), so this risk is low but not zero.
* The crash isolation ladder B2-B5 requires module reloads with different
  rx_refill_wm/rx_clear_status values; the capture script does not change
  module parameters (it never writes module state). The Lead runs those rungs
  from the existing fire recipe.
* Monitor-mode phase 0b depends on the MT7922 supporting monitor+managed
  concurrency; if it does not, the discriminator is unavailable, not negative.

## 10. Open questions this experiment can close or narrow

1. Does the MAC's sticky interrupt status word gain any RX bit (2/5/10) while
   the client's auth frames are on air at -33 dBm? (H1 vs H2)
2. Does the D2H catalogue gain any g1 id13 frame during the same window?
   (device/firmware reception and event generation)
3. Does the firmware page/mask/staging deviate during a no-drain capture?
   (whether the exception needs drain writes at all)
4. Does the AP ACK the client's authentication frames (phase 0b)? (radio-level
   reception independent of the firmware event path)
5. Do the RX ring buffers stay byte-identical across the trigger window?
   (no unexpected MAC write into host memory)

