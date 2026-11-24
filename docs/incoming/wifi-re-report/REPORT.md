# The Hi5622V100 Wi-Fi / chip protocol: a consolidated reverse-engineering report

**Project:** Cudy WR3000 v2.0 (luofu v2 board) bench reversal - Hi5671YV200 SoC + Hi5622v100 Wi-Fi
endpoint, stock OpenWrt 22.03.6 / kernel 5.10.201, vendor firmware 928,920 B.

**Document:** `wifi-re-report/REPORT.md`, umbrella narrative, **v5**. **Date:** 2026-11-29.
**State cut-off:** `mem-entries.md` entry 2026-11-29 01:00 (the newest ledger entry at this
revision); superseded claims are catalogued in section 8. **Status:** the full duplex HCC
protocol, the operational message lap, the complete six-message calibration boot (C2, automated
PASS), the first real Wi-Fi interface on our own image (cfg80211 wiphy registration + VAP netdev
lifecycle + real `.start_ap`), the first real firmware management frames, the first real
firmware **scan results** (mode-1 VAP base; six channel reports + six beacons, later 12 BSS
entries decoded), the **MAC data-plane host setup + TXQ-table address sync** (automated E1 PASS),
the **first data-plane frame published to the MAC TX ring** (POST-ONLY), the **one-command full
bring-up matrix** (boot/C2/D1/D2/E1 = ALL PASS) and - the v4 milestone - the **first live Wi-Fi AP
on our clean-room stack**: hostapd `wlan0: AP-ENABLED`, SSID `cudy-ap-test`, with the four-message AP
start consumed by the firmware. The ABI-correct image was then **flashed** (2026-11-27 06:30):
kernel read-back matched the slot hash, the ABI receipt matched the kmod pin (`6.18.60~6f995278...`),
the rootfs-native modules load without `/tmp`, and the AP came up again on the flashed image
(`AP-ENABLED`). The AP state was still live minutes later and the firmware's D2H stream changed with
the AP start (a new `g1 id0x23` 24 B event family) - indirect evidence the firmware is running the
AP, but not an on-air beacon proof. **Then the v5 static-RE wave (2026-11-28) closed the remaining
protocol gaps**, several now hardware-fired: the MAC RX live-bank correction with `rx_arm` proving
the RX consumer (**PASS**, exact vendor mirror), the **WPA2-AP GTK install proven** (key_add +
set_default_key consumed; PTK remains RX-gated), the **five-message vendor AP-start order**
(channel config `0x0124` first, VAP start after `ap_cfg`) reaching `AP-ENABLED`, the **full
13-channel scan question closed as structurally impossible** (honest ceiling 6 channels/request, 7-8
with the H1 pacing config), the STA/5GHz/HT/VHT model decoded (patch fail-closed, not fired), the
**MAC TX completion proven** (the missing device-consumer step was the vendor TXQ-table publish), the
**E2 AP restart in one boot**, and a **40-minute AP soak (242/242 samples golden)**. The wave
culminated in the **final all-patch module**: one clean-room module carrying every feature was
validated in **one boot** - vendor five-message AP order + WPA2-AP GTK + TX COMPLETED + RX arm all
PASS, module **308,080 B sha256 `5b587949...`**, 47 parameters, 0 warnings (6.18/F20). Still not
validated: beaconing over the air, any client association, an RX frame capture with on-air traffic,
a fired STA join, the 5 GHz band config and baking the final module into the image. The firmware
IRQ/handler layer stays
closed (H1: PD4 is consume-only) and every bench result runs on the PD4-patched research firmware.
Detail sources remain the lane reports listed in section 12; this document is the publication-grade
synthesis, not a replacement.

**Lane / task:** wifi-re-report, shared task `task-129` **v5 addendum** (v4 was the task-129
original; v3 was `task-120`, v2 `task-116`, v1 `task-109`). Host-only: no device, UART, TFTP or
network contact was made while writing it.
**Safety:** nothing in this report authorises a bench fire; every hardware session cited here was
attended and followed the project's recovery-door rules.

---

## 0. Document control

### 0.1 How to read this report

This report is about **what the chip does and how our clean-room host drives it**, ordered as the
investigation actually happened. It is written so that a reader with the per-lane reports open can
move between the narrative and the evidence in both directions. For every claim there is an
evidence marker and, in [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md), the artifact or ledger entry that
supports it.

Three audiences are served: (a) the project itself, as the reference narrative after a long
multi-lane investigation; (b) reviewers of the public notebook who need to distinguish verified
from speculative; (c) future maintainers who must not repeat the expensive mistakes documented in
section 8.

### 0.2 Companion files

| file | content |
| --- | --- |
| [TABLES.md](TABLES.md) | address maps, ROM map, golden state, protocol tables, frame hex dumps, patch recipes, timing constants |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim -> marker -> artifact/ledger entry, grouped by report section |
| [REPRODUCE.md](REPRODUCE.md) | static reproduction commands and bench-ladder pointers |
| [README.md](README.md) | lane index and ground rules |
| [tools/verify_report.py](tools/verify_report.py) | device-free verifier for this report |

### 0.3 Evidence markers

| marker | meaning | example |
| --- | --- | --- |
| `[V]` | re-verified by the owning lane from a named local artifact (vendor module, firmware blob, ROM dump, image) with a committed tool | "the 12-byte header is built by `hcc_msg_alloc`" [V] |
| `[V*]` | measured on the bench in an attended session by an earlier lane; cited by ledger date | "golden page = 0x86807FEF/7/2/0xABCD" [V*] |
| `[I]` | inference from decoded code/data, not directly measured | "the wifi-custom block is PHY/radio configuration" [I] |
| `[U]` | unknown; needs a device read, ROM/vendor evidence, or is outside the instrument's reach | "the relationship between the two firmware hcc objects" [U] |

A single sentence may carry more than one marker when it mixes measured and inferred parts.
**No claim in this report is stronger than its marker.** In particular, "the protocol works" means
*the two messages we sent were consumed and answered* - not that the whole Wi-Fi stack works.

### 0.4 Provenance and non-goals

The report synthesises committed lane deliverables under `opensource/docs/incoming/`. It did not
re-read the vendor blobs to invent new facts; where a claim needed re-checking, the owning lane's
committed self-testing tool was re-run (section 9.1). The non-goals are the same as the project's:
this is not an upstream OpenWrt submission, not a shippable driver, and not a claim of clean-room
reimplementation of the closed firmware.

### 0.5 What changed in v2

v1 stopped at the 2026-11-25 06:00 ledger entry. v2 integrates the later milestones of the same
day; the earlier material is unchanged except where a later result superseded it (section 8):

1. the **cfg80211 bench image** was built, flashed to kernelb (full-slot hash - see T21) and
   booted on the box (section 6.3);
2. the **first real Wi-Fi interface on our own image**: wiphy registration gated on the ready
   handshake, `iw` enumerates phy0, and an nl80211 VAP netdev is created and deleted (M0/M1
   hardware-validated; finding F8, table T22);
3. **ladder C1**: the firmware answered the first calibration message (298 B g0 id2 key 0x0101
   request -> 298 B g1 id3 config-sync answer; finding F7, table T23);
4. the **ETE DR (D2H) A->B copy** host lever was measured negative (E1a/E1b); the start condition
   is device/ROM state and the direct device-ring read is the supported receive path (section
   6.5);
5. the **data-plane architecture** is mapped: a separate MAC host-interface ring block (slot 5),
   the TXQ-table address sync, the per-TID MSDU-info TX entries and the RX free/completion rings
   (finding F9, table T24);
6. the **PD4 research patch** is audited as a minimal, bench-only instrument: byte-identical
   rebuild (two segments, 68 patched bytes), independently re-verified cave proof, the unresolved
   H1/H2 sentinel question, ranked diagnostic variants, and no proven vendor-clean mailbox
   delivery (finding F10, table T25, `wifi-fw-audit/REPORT.md`);
7. the bench evidence chain gains the 2026-11-25 07:40-15:00 sessions (section 7), the
   corrections register gains C17-C26 (section 8), and the verification, limits, publication and
   lane-map sections are updated (sections 9-12).

### 0.6 What changed in v3

v2 stopped at the 2026-11-25 15:00 ledger entry. v3 integrates the 2026-11-25 16:30 - 2026-11-26
06:30 milestones, i.e. the wave that turned the cfg80211 management interface into real firmware
traffic. The chip-protocol facts of v1/v2 are unchanged; where a v2 statement is now superseded it
is recorded in section 8 (C29-C33).

1. **The calibration boot completes.** Ladder C2 answered all six 298 B calibration messages (six
   `g1 id3` answers, `rd_catalog` count 7, `last_total 0x012A`); the automated C2 phase later
   passed **54/54** checks in one command (section 6.2, table T23).
2. **Real firmware management frames are on the wire.** The first VAP `user_add` (g0 id7, 180 B),
   the `user_add_notify` (g0 id8, 180 B) and the first scan request (g0 id0xC, 520 B) were posted
   and consumed in 0 polls; the firmware committed the VAP (`user_id=0 vap_id=0`) but returned no
   scan results, which shifts the wall to the per-radio VAP start object (finding F11, section 6.7,
   tables T26/T27).
3. **The MAC data plane is no longer host-side paper only.** The clean driver's `dp_probe` /
   `dp_setup` / `dp_rings` / `dp_txq_sync` extension sets up the slot-5 TX/RX rings and sends
   the vendor's own TXQ-table address sync (g0 id1, key `0x0320`); the automated E1 phase passed
   **37/37** checks with no exception (sections 6.4/6.8, table T26), still without a frame on the
   rings.
4. **One command reproduces the whole bring-up.** `phase_matrix.py --phase all --reboot-between`
   returned boot **PASS**, C2 **PASS**, D1 **PASS**, D2 **BLOCKED-DRIVER**, E1 **PASS** with
   per-phase evidence and bounded reboots (section 6.8, table T28).
5. **H1 is settled.** The decisive read-only pack shows PD4's handler-table pointer keeps its
   static value and the live handler table is all zero, so the dispatcher handlers never execute
   under PD4; the working duplex is the non-mailbox ETE/polled H2D path (section 6.6, table T25,
   correction C29). PD4 remains a research-only instrument.
6. The corrections register gains C29-C33 (section 8), the evidence chain gains the 2026-11-25
   16:30 - 2026-11-26 06:30 sessions (section 7), tables **T26-T28** are added, the evidence index
   gains **EV-196..EV-244**, and the verification, limits, open questions, publication and
   lane-map sections are updated (sections 9-13).

### 0.7 What changed in v4

v3 stopped at the 2026-11-26 06:30 ledger entry. v4 integrates the 2026-11-26 08:20 - 2026-11-27
09:00 milestones, the wave that turned the reverse-engineered AP path into the first live Wi-Fi AP
on our own stack, flashed the ABI-correct image and reproduced the AP on it. The chip-protocol
facts of v1-v3 are unchanged; where a v3 statement is now superseded it is recorded in section 8
(C34-C45).

1. **The scan wall is gone.** The mode-1 VAP base (`g0 id1` keys `0x0064`/`0x0065`) creates the
   firmware VAP object the scan handler needs; the firmware produced the project's first real scan
   results (six channel reports + six beacons, later 12 BSS entries decoded from `g1 id15`) and the
   D2 phase flipped to **PASS** (section 6.9, correction C34).
2. **The matrix is all-PASS.** The one-command matrix now returns boot/C2/D1/D2/E1 = PASS/PASS/
   PASS/PASS/PASS (section 6.8, T28 updated, correction C35).
3. **The hostapd path is reversed as capabilities.** Eight driver-side gates were found and fixed
   one bench fire at a time - the missing ucode files, the fail-closed `.connect`, `.mgmt_tx`,
   `wiphy->mgmt_stypes`, `WIPHY_FLAG_REPORTS_OBSS`, a paired `.disconnect` (the `wiphy_new_nm`
   WARN), and the AP-type interface + mode-1 VAP base prerequisites (section 6.10, table T30).
4. **The first live Wi-Fi AP.** On a fresh golden boot, hostapd reached the driver's `.start_ap`,
   the firmware consumed the four-message AP start (vap_add 48 B -> vap_start 40 B -> ap_ssid
   64 B -> ap_cfg 184 B) in 0 polls each, and hostapd printed **`wlan0: AP-ENABLED`** with SSID
   `cudy-ap-test` (section 6.11, table T29, correction C36). The earlier crash was a missing
   firmware VAP object, not a malformed frame.
5. **The first data-plane frame reached the MAC ring.** Merging the E1 data plane into the mode-1
   provider gave the first TX frame on the MAC MSDU ring: `dp_probe` (a READ) -> `dp_setup` ->
   `h2d_txq_addr_sync` -> `dp_tx_frame` published `wptr 0x0000 -> 0x0001`; the completion read-back
   is **POST-ONLY** (section 6.12, table T31, correction C41).
6. **Userspace and image track advanced, and the image was flashed.** Native `iw` 6.17 /
   `iwinfo` / `wireless-regdb` are deployed persistently through the T0 zero-flash path (49/49
   gates, 0 failed); the hostapd package set is built and host-verified (88/88 checks + 14/14
   selftests); the bench image was reproduced byte-for-byte with an ABI-correct rootfs
   (sections 7, 9, 11). **Then the ABI-correct image was flashed (2026-11-27 06:30)**: kernel
   read-back matched the slot hash, `ubiformat` OK, active pair untouched, handover completed
   (`bootflag=b`, `boot_reg=0x21`), the box booted our rootfsb image and the post-boot ABI receipt
   matched the kmod pin - the kmod-ABI mismatch is **closed**. The AP came up again on the flashed
   image (`AP-ENABLED`), reproduced from rootfs-native modules (correction C44).
7. **H1 is re-affirmed as the standing limit.** PD4 remains consume-only; every v4 bench fire used
   the PD4-patched research firmware, and the first live AP does not change the upstream verdict
   (section 6.6, correction C42).
8. **AP persistence and a new firmware event family (2026-11-27 09:00).** The AP stayed live for
   minutes after start, and the D2H catalogue gained a family absent at boot - `g1 id0x23` (35)
   count=6, 24 B (plus `g1 id3` count=20, 466 B responses) - likely AP/beacon-period activity [I].
   It is indirect evidence the firmware is running AP activity; the on-air beacon still needs an
   external observer (section 6.11, correction C45).
9. The corrections register gains C34-C45 (section 8), the evidence chain gains the 2026-11-26
   09:00 - 2026-11-27 09:00 sessions (section 7), tables **T29-T31** are added and T28 is updated,
   the evidence index gains **EV-245..EV-277**, and the verification, limits, open questions,
   publication and lane-map sections are updated (sections 9-13).

### 0.8 What changed in v5

v4 stopped at the 2026-11-27 09:00 ledger entry. v5 integrates the **static-RE wave of 2026-11-28**
(five host-only lanes) and the bench fires that turned its predictions into hardware facts. The
chip-protocol facts of v1-v4 are unchanged; where a v4 statement is now superseded it is recorded
in section 8 (C46-C55).

1. **The RX mystery is solved: we were polling the wrong block.** The live MAC ring pointers are in
   a separate mapped block, **CA 0x40050000** (+0x00 status, +0x08 norm free-ring rptr, +0x0c small
   rptr, +0x10 completion wptr); the config block's +0x4c/+0x50/+0x54 (CA 0x40040000) are never read
   by the vendor and stay zero. The completion path (entry = devVA(buffer+4), 36 B header at
   buffer+4, len at +0x1a, MPDU at +0x28) is decoded, and the fired `dp_rx_arm` printed the live
   bank matching vendor stock exactly (**status 0x182, mask 0xfffffcc1, norm/small rptr 0x20**):
   the MAC RX consumer is **proven**; captures wait for on-air traffic (section 6.13, F15, C46/C47).
2. **WPA2-AP is live: the GTK install is hardware-proven.** The vendor key ops are g0 id1 config
   events - **cmd 0x0078** (add_key, 0x44 body), **0x0079** (set_default_key, 0x28), **0x007A**
   (del_key, 8 B) - to firmware workers CA **0x0A5825 / 0x0A5C9D / 0x0A4271**; no MAC-register
   sequence. The fired WPA2 hostapd AP posted `key_add` (96 B, idx=1, cipher 0xfac04, CCMP) and
   `key_default` (68 B, multi=1), both consumed in 0 polls: the GTK is installed and selected; the
   4-way PTK remains RX-gated (section 6.14, F16, C50).
3. **The vendor AP start is FIVE messages, not four.** Channel config **0x0124 FIRST** -> SSID
   `0x0005` -> auth `0x006D` (WAL only) -> AP config `0x0127` -> **VAP start `0x0065` AFTER
   `ap_cfg`**; the host never posts a beacon template (the firmware SMAC beacons autonomously).
   The prior order (no channel config, VAP start before `ap_cfg`) was the static candidate for the
   missing on-air beacons; the fired vendor order reached `AP-ENABLED` with the channel
   programmed before the AP config (section 6.15, F17, C48/C53).
4. **The full 13-channel scan is structurally closed.** The g0 id10 per-slot array is a BA-relay,
   not a channel plan: the scan engine consumes **slot 0 only**, and no subtype-1 field writes the
   count/end byte (+0xFE) - so extra slot configs cannot widen a request. Honest ceiling:
   **6 channels/request**, **7-8** with the H1 pacing config (off by default); a clean negative
   result (section 6.16, F18, C49).
5. **STA / 5 GHz / HT-VHT are mapped.** STA connect/disconnect are WAL cfg-events (**0x0123**,
   108 B body; **0x00d0**, 16 B), the firmware performs auth/assoc (connect-SME driver), 5 GHz is
   one wiphy per **band** (single host MAC, two RF chips; 14/29-channel vendor tables), HT cap
   **0x0062**, VHT 2SS MCS0-9 with VHT cap **0x00400000** (2.4 GHz) / **0x00400064** (5 GHz, 160
   MHz + SGI); fail-closed patch with `sta_join`/`ht_mode`/`vht_cap` (section 6.17, F19, C51/C52).
6. **The TX-POST-ONLY gap is solved and the AP restarts.** The missing device-consumer step was the
   vendor TXQ-table queue-state publish after the ring push; with it the MAC consumed the frame
   (`COMPLETED (MAC)`, device rptr == published wptr). The AP also restarts in one boot (E2) and
   a 40-minute soak on the final R4 combined module gave **242/242 golden samples** (6.11/6.12,
   C54/C55).
7. **The final all-patch module validates everything in one boot (2026-11-29 01:00).** The
   stability lane built one clean-room module carrying all static-wave features (stripped
   **308,080 B sha256 `5b587949...`**, 47 parameters, 0 warnings, opensource `008aca5`) and fired it
   on a fresh golden boot: vendor five-message AP order + hostapd `AP-ENABLED` with the WPA2 config,
   WPA2 GTK key_add/set_default consumed, TX **COMPLETED**, RX arm PASS (vendor mirror), page golden
   / mask 0. That is the project's culminating device-level validation (section 6.18, F20, T38).
8. The corrections register gains **C46-C56** (section 8), findings **F15-F20** (section 4),
   sections **6.13-6.18** (the static-RE wave + the final module), tables **T32-T38**, evidence-chain
   rows for 2026-11-28/29, and the evidence index gains **EV-278..EV-308**; limits, open questions,
   publication, lane map and glossary are updated (sections 9-13).

---

## 1. Executive summary

### 1.1 The result

We can boot the Hi5622V100 Wi-Fi endpoint from our own clean-room host, bring its closed firmware
to the exact state a working vendor boot reaches, receive its device-to-host protocol frames, and
exchange real host-to-device protocol messages that the firmware processes and answers - all on
our own kernel/driver, using no vendor host code.

The complete chain, each link hardware-validated:

```
SoC GPIO endpoint reset pulse
  -> chip mask ROM S0/S1 runs
  -> ROM <-> image boot handshake completes (0xCEED gate preserved; page +8 = 2)
  -> firmware golden boot (page 0x86807FEF/7/2/0xABCD, state 1, mask 0)
  -> device->host ready handshake decoded (g4 id1 + g4 id2, payload 0x01060850)
  -> host->device context-create (g0 id1, key 0x64, type 1) allocates ctx+0x190
  -> host->device TXQ config sync (g0 id1, cmd 0xD8) consumed with ZERO exceptions
  -> firmware responds with new D2H frames (g1 id3, g1 id0x12)
  -> host->device calibration set_rssi_param (g0 id2, key 0x0101, 298 B) consumed
  -> firmware answers with a 298 B g1 id3 config-sync frame (ladder C1)
  -> cfg80211 wiphy registers only after the ready handshake; iw enumerates Wiphy phy0
  -> nl80211 VAP wlan0 created and deleted on our own image (cfg80211 M0/M1)
  -> C2: six calibration messages (298 B each) sent and ALL SIX answered (six g1 id3 298 B)
  -> real firmware VAP create: user_add (g0 id7, 180 B) + user_add_notify (g0 id8, 180 B)
     consumed in 0 polls; the firmware commits the VAP (user_id=0, vap_id=0)
  -> first real scan request (g0 id0xC, 520 B) consumed in 0 polls
  -> mode-1 VAP base (g0 id1 keys 0x0064/0x0065) creates the firmware VAP object; the first real
     scan results: six 124 B g1 id17 channel reports + six 509 B g1 id15 beacons (12 BSS decoded)
  -> hostapd NEW_BEACON -> cfg80211 .start_ap -> the four-message AP start posted in order
     (vap_add 48 B -> vap_start 40 B -> ap_ssid 64 B -> ap_cfg 184 B), consumed in 0 polls each
  -> hostapd "wlan0: AP-ENABLED" with SSID cudy-ap-test: the first live Wi-Fi AP on our stack
  -> MAC data-plane host setup (dp_probe/dp_setup/dp_rings) + TXQ-table address sync
     (g0 id1, key 0x0320) accepted: automated E1 PASS 37/37, no exception
  -> first data-plane frame published to the MAC TX MSDU ring (POST-ONLY: wptr 0->1,
     completion_pending=1)
  -> one command runs the whole matrix: boot/C2/D1/D2/E1 = PASS PASS PASS PASS PASS
  -> the ABI-correct image is flashed (slot read-back + ABI receipt match); rootfs-native modules
     load and the AP comes up AGAIN on the flashed image: hostapd "wlan0: AP-ENABLED"
  -> v5 static wave (2026-11-28): RX live bank CA 0x40050000 read + dp_rx_arm PASS (consumer
     proven); MAC TX COMPLETED via the TXQ-table publish; five-message vendor AP-start order
     (channel 0x0124 first, VAP start after ap_cfg) -> AP-ENABLED; WPA2 GTK key_add/key_default
     consumed; full-13 scan closed structurally (ceiling 6, 7-8 with H1); AP restart (E2);
     40-min soak 242/242 golden; STA/5GHz/HT-VHT decoded and patch fail-closed
  -> final all-patch module (2026-11-29 01:00): ONE boot with every feature live - vendor
     five-message AP order + WPA2-AP GTK + TX COMPLETED + RX arm all PASS (module 308,080 B
     sha256 5b587949..., 47 params); page golden / mask 0
```

That is the project's Wi-Fi-side core RE goal: **the full duplex HCC protocol works on a
clean-room host**, the first real Wi-Fi interface and the first live AP control plane exist on our
own image, the firmware consumes our hand-built VAP/scan/AP/WPA2 frames and produces real scan
results, the MAC RX consumer and TX completion are proven at the device level, and the vendor AP
sequence is reproduced exactly. The remaining work is the over-the-air proof (beaconing/
association observed externally), RX captures with live traffic, a fired STA join, the all-patch
single-module validation and the still-closed firmware/IRQ layer (sections 6.9-6.17 and 10).

### 1.2 What is proven, and what is not

| claim | status |
| --- | --- |
| host can reset and boot the chip's mask ROM (S0/S1) | **proven** [V*] |
| ROM<->image handshake contract (0xCEED, page +8 = 2, entry A/B) | **proven** [V]/[V*] |
| golden firmware state achievable repeatedly | **proven** [V*] |
| device->host ready handshake and other D2H frames readable | **proven** [V*] |
| H2D transport (SR ring commit + mailbox announce) | **proven** [V*] |
| firmware processes real H2D messages and answers | **proven** for the two ordered messages, the calibration class and the VAP/scan management-request classes [V*] |
| calibration exchange (g0 id2 key 0x0101 -> g1 id3 298 B) | **proven** as a protocol exchange and completed for the whole six-message set (C2: all six answered, 298 B each); the answer bodies were counted, not decoded [V*] |
| real firmware management frames + real scan results | **proven**: user_add g0 id7 180 B, user_add_notify g0 id8 180 B and scan_req g0 id0xC 520 B consumed in 0 polls; after the mode-1 VAP base the firmware returned real scan results (six g1 id17 124 B channel reports + six g1 id15 509 B beacons in a 5 s window; 12 BSS entries decoded from a 13-channel run) [V*] |
| first live Wi-Fi AP (control plane) | **proven**: hostapd `AP-ENABLED`, `type AP, ssid cudy-ap-test` on the flashed image; the four-message AP start (vap_add 48 B, vap_start 40 B, ap_ssid 64 B, ap_cfg 184 B) consumed in 0 polls each. **v5**: the exact **five-message vendor order** (channel `0x0124` first -> SSID -> auth -> `ap_cfg` -> VAP start after `ap_cfg`) also reaches `AP-ENABLED`; the AP restarts in one boot (E2) and a 40-min soak was 242/242 golden [V*] |
| one-command full bring-up matrix | **proven**: boot/C2/D1/D2/E1 = PASS/PASS/PASS/PASS/PASS in one command with bounded between-phase reboots [V*] |
| cfg80211 management interface (wiphy + VAP netdev + real `.start_ap`) | **proven** on our own image (M0/M1 + the AP-start provider); after the mode-1 VAP base a firmware VAP object exists (`ctx0+0x190` nonzero) [V*] |
| firmware IRQ/thread layer is alive on our boot | **not proven, and now explained** - the PD3/PD4 research patch services the mailbox without the IRQ layer; the H1 pack shows PD4 dispatches through an empty handler table (consume-only), so the duplex runs on the non-mailbox ETE/polled H2D path [V*] |
| ETE engine DR (D2H) A->B copy | **host lever proven negative** - the only host-owned delta was tested (E1a/E1b); start is device/ROM state; direct read is the supported RX path [V*]/[I] |
| firmware VAP/scan/AP control messages | **proven through AP start and WPA2 keys** - VAP add/start, scan request/results, the AP-config sequence and the **WPA2 GTK key messages** (key_add `0x0078` + set_default_key `0x0079`) are firmware-accepted; beaconing over the air, client join/association and the 4-way PTK remain open (PTK is RX-gated) [V*]/[U] |
| MAC frame data path (TX/RX frames) | **TX completion proven, RX consumer proven** - the first frame was published POST-ONLY, then the vendor **TXQ-table queue-state publish** was added and the MAC consumed it (`COMPLETED (MAC)`, device rptr == published wptr); the **RX live bank** (CA 0x40050000) was read with `dp_rx_arm` matching vendor stock exactly (status 0x182, mask 0xfffffcc1, norm/small rptr 0x20), so the MAC RX consumer is armed/proven; no RX frame capture yet (needs on-air traffic) and no on-air observation [V]/[V*] |
| beaconing / association / working link | **not proven** - `AP-ENABLED` is a host/control-plane fact; the v5 five-message order programs the module channel before the AP config and the firmware SMAC owns beacon generation (no host template), but no beacon has been observed on the air, no client has associated, and the AP-side auth/assoc path still needs host mgmt TX + MAC RX (mgmt ops stay fail-closed) [I]/[U] |
| full 13-channel scan | **structurally impossible in one request** - the g0 id10 per-slot array is a BA relay and the scan engine consumes slot 0 only; honest ceiling 6 channels/request, 7-8 with the H1 pacing config (off by default); 13 channels only via host batching (negative result documented) [V]/[V*] |
| WPA2-AP security | **GTK proven**: hostapd WPA2-AP reaches `AP-ENABLED` and the group key (idx 1, CCMP, 16 B) is installed + selected via two consumed firmware frames; the 4-way PTK and client association stay RX-gated [V*] |
| STA / 5 GHz / HT-VHT | **static model decoded, patch fail-closed, not fired as a connect rung** - connect is WAL cfg-event `0x0123` (108 B), disconnect `0x00d0` (16 B), firmware owns auth/assoc, 5 GHz is one wiphy per band (14/29-channel tables), HT cap `0x0062`, VHT 2SS with `0x00400000`/`0x00400064`; `sta_join`/`ht_mode`/`vht_cap` default off. The patch is in the final all-patch module but `sta_join=0` kept the join unexercised [V]/[U] |
| final all-patch module (one boot) | **proven**: one clean-room module (**308,080 B sha256 `5b587949...`**, 47 params) validated in a single fresh boot - vendor five-message AP order + WPA2-AP GTK + TX COMPLETED + RX arm all PASS; page golden/mask 0 (section 6.18, F20, T38) [V*] |
| upstreamable clean-room driver | **blocked** by the closed firmware blob and the unmatched vendor GPL request (section 11) |
| PD4-patched vendor firmware | **research instrument only** - never a shipping change (section 6.6) |

### 1.3 The breakthrough ladder

The investigation reached the result through thirty-three hardware-validated breakthroughs; each
is also a correction of the then-current model (section 8 gives the full register):

| # | date | breakthrough |
| ---: | --- | --- |
| 1 | 2026-11-22 | clean-room transport reproduces the PCIe lifecycle: six iATU viewports, contiguous 928,920 B upload, D2H announce handling [V*] |
| 2 | 2026-11-24 01:15 | the missing host action is a SoC GPIO endpoint reset pulse; with it the mask ROM S0/S1 runs and the ROM handshake can complete - but flakily [V*] |
| 3 | 2026-11-24 09:20 | the chip mask ROM is host-readable; the 16 KiB ROM is dumped and reversed (2,073 B content) [V*]/[V] |
| 4 | 2026-11-24 10:00 | root cause of the S1 stall: **our own 0xCECE gate writes clobber the ROM's 0xCEED handshake**; the boot protocol becomes release-only [V]/[V*] |
| 5 | 2026-11-24 10:40 | **GP8**: firmware boots exactly like the vendor (golden page, state 1, mask 0) [V*] |
| 6 | 2026-11-24 13:10-14:00 | D2H frames are real and readable directly from device rings; the complete ready handshake is received [V*] |
| 7 | 2026-11-24 22:10 | **PD3**: a guarded firmware patch calls the firmware's own H2D dispatcher from the boot-task epilogue, so the firmware mailbox consumer works without the dead IRQ layer [V*] |
| 8 | 2026-11-25 05:10 | **ordered operational loop**: context-create type 1 -> TXQ config sync, both consumed with zero exceptions, firmware responds [V*] |
| 9 | 2026-11-25 02:10 | **PD4** moves the dispatcher stub to a genuinely dead static-data cave (the exception-staging area no longer clobbers it), which enables the context-precondition analysis [V*] |
| 10 | 2026-11-25 10:10 | the **ETE DR wake lever is closed**: the only host-owned state delta (published index) was tested negative; the A->B copy start is device/ROM state, so the direct group-A read stays the supported RX path [V*] |
| 11 | 2026-11-25 12:30 | **ladder C1**: the firmware answers the first calibration message (298 B g0 id2 key 0x0101 -> 298 B g1 id3) [V*] |
| 12 | 2026-11-25 15:00 | **first real Wi-Fi interface on our own image**: cfg80211 image flashed (slot 1794a531), wiphy registered after the ready handshake, iw enumerates phy0, VAP wlan0 create/delete via nl80211 (M0/M1) [V*] |
| 13 | 2026-11-25 16:30 | **ladder C2**: all six calibration messages answered (six 298 B g1 id3); the vendor calibration boot runs end-to-end, and the automated C2 phase passes 54/54 [V*] |
| 14 | 2026-11-25 21:30 | **H1 confirmed**: PD4's live handler table is empty (the static sentinel pointer is unchanged), so its dispatcher handlers never execute; the duplex path is non-mailbox ETE/polled H2D [V*] |
| 15 | 2026-11-26 05:30 | **one-command bring-up matrix**: boot/C2/D1/D2/E1 = PASS/PASS/PASS/BLOCKED-DRIVER/PASS with bounded reboot orchestration and per-phase evidence [V*] |
| 16 | 2026-11-26 06:30 | **first real firmware management frames**: VAP user_add + notify + the first scan request posted and consumed in 0 polls; the scan-response wall is the firmware VAP start object [V*] |
| 17 | 2026-11-26 12:00 | **T0 userspace deploy PASS**: native `iw` 6.17, `iwinfo`, `libiwinfo` and `wireless-regdb` installed persistently from the rootfs overlay (49/49 gates, 0 failed); modules stay in `/tmp` until the kmod-ABI path is taken [V*] |
| 18 | 2026-11-26 14:00 | **first real firmware scan results**: mode-1 VAP create (`g0 id1` key 0x0064 48 B -> 0x0065 40 B) then scan_req -> six 124 B `g1 id17` channel reports + six 509 B `g1 id15` beacons; the v3 VAP-start wall is lifted [V*] |
| 19 | 2026-11-26 16:30 | **first full BSS decode**: 12 BSS entries parsed from `g1 id15` beacons (SSID/DS/HT/RSN/WMM/country) into cfg80211; the firmware's real pacing (~3.5 s/channel) bounds a 13-channel run [V*] |
| 20 | 2026-11-26 15:00/17:00 | **D2 TRUE PASS + one-command all-PASS matrix**: boot/C2/D1/D2/E1 = PASS/PASS/PASS/PASS/PASS with per-phase evidence (`build/wifi-bench-phase-matrix/20261124T094716Z/`) [V*] |
| 21 | 2026-11-26 18:00 | **ABI-correct image reproduced byte-for-byte**: container `df6a78c3...`, slice `13e81214...`, slot `1794a531...`, rootfs `62344e80...`; the image rootfs apk db carries the kernel package the kmods pin (`6f995278...`), so the fix is a kernel+rootfs flash from one build - the runbook was ready; the flash was executed on 2026-11-27 06:30 (rung 25) [V] |
| 22 | 2026-11-26 20:30 - 2026-11-27 01:30 | **hostapd runs on the image and the AP-setup capability ladder is climbed**: ucode files, fail-closed `.connect`/`.disconnect`, `.mgmt_tx`, `wiphy->mgmt_stypes`, `WIPHY_FLAG_REPORTS_OBSS`; each gate was made visible in dmesg and fixed by the ap-start lane [V*] |
| 23 | 2026-11-27 02:30 | **first data-plane frame on the MAC TX ring**: `dp_probe` (READ) -> `dp_setup` -> key-0x0320 sync -> `dp_tx_frame` published (`wptr 0 -> 1`), completion_pending=1 -> POST-ONLY [V*] |
| 24 | 2026-11-27 04:30 | **THE FIRST LIVE WI-FI AP ON OUR CLEAN-ROOM STACK**: fresh golden boot -> REPORTS_OBSS + mgmt_stypes -> start_ap -> four-message AP start consumed -> hostapd `wlan0: AP-ENABLED`, `type AP, ssid cudy-ap-test` [V*] |
| 25 | 2026-11-27 06:30 | **the ABI-correct image is flashed and the AP runs on it**: slot read-back `1794a531...` matched, `ubiformat` OK, active pair untouched, handover `bootflag=b`/`boot_reg=0x21`, boot 6.18.60 from rootfsb; post-boot `apk query kernel` = `6f995278...` (ABI mismatch closed); rootfs-native modules load; hostapd `AP-ENABLED` again with all four AP-start frames consumed in 0 polls [V*] |
| 26 | 2026-11-27 09:00 | **AP persistence + new firmware event family**: the AP is still live minutes after start (hostapd processes alive, `type AP, ssid cudy-ap-test`, no exceptions), and the D2H catalogue gained a family absent at boot: **`g1 id0x23` (35) count=6, 24 B**, plus `g1 id3` count=20 (466 B responses). Indirect evidence the firmware is running AP activity; beacons still need an external observer [V*] |
| 27 | 2026-11-28 01:00 | **MAC TX COMPLETED (POST-ONLY solved)**: the missing device-consumer step was the vendor **TXQ-table queue-state publish** after the ring push (per-TID count/bytes/SN + timestamp); with it the MAC consumed the frame - device rptr == published wptr, `COMPLETED (MAC)` [V*] |
| 28 | 2026-11-28 13:30 | **final R4 combined validation + 40-min soak**: one boot AP-ENABLED, E2 restart AP-ENABLED again, TX COMPLETED; the 60-min soak was stopped at 242 samples (~40 min) with **all samples golden:1/hostapd=4** [V*] |
| 29 | 2026-11-28 17:00 | **full 13-channel scan closed structurally**: g0 id10 is a BA relay; the scan engine consumes slot 0 only and no subtype-1 field writes the count byte - ceiling 6 channels/request (7-8 with H1 pacing); clean negative result [V]/[V*] |
| 30 | 2026-11-28 21:00 | **RX arm PASSED**: `dp_rx_arm` on a fresh AP boot read the live bank CA 0x40050000 = status 0x182, mask 0xfffffcc1, norm/small rptr 0x20 - **exact vendor stock mirror**; the MAC RX consumer is proven (drain has no completions yet - no on-air traffic) [V*] |
| 31 | 2026-11-28 22:00 | **five-message vendor AP-start order implemented and AP-ENABLED**: vap_add -> `ap_chan` (32 B, band=0 channel=6, channel context programmed BEFORE `ap_cfg`) -> ap_ssid -> ap_cfg -> `ap_vap_start` AFTER ap_cfg; hostapd `AP-ENABLED`, page golden/mask 0 [V*] |
| 32 | 2026-11-28 23:00 | **WPA2-AP GTK install PROVEN**: WPA2 hostapd AP reaches `AP-ENABLED`; `key_add` (96 B, idx=1, cipher 0xfac04) + `key_default` (68 B, multi=1) consumed in 0 polls by firmware workers CA 0x0A5825/0x0A5C9D - the M3 unblock is live, PTK remains RX-gated [V*] |
| 33 | 2026-11-29 01:00 | **FINAL ALL-PATCH MODULE VALIDATED IN ONE BOOT**: one clean-room module (stripped 308,080 B sha256 `5b587949...`, 47 params, 0 warnings) carries every feature - vendor five-message AP order + WPA2-AP GTK + TX COMPLETED + RX arm all PASS on a fresh golden boot, page golden/mask 0 [V*] |

### 1.4 Key numbers

| quantity | value | evidence |
| --- | ---: | --- |
| firmware image | 928,920 B, sha256 `7fc87e20...ef311b` (pinned local blob) | [V] |
| mask ROM content | 2,073 B of 16,384 B window; sha256 `fcfd464f...8faa4` (local dump) | [V]* |
| BAR0 windows | 6 inbound iATU viewports, 16 MiB total | [V*] |
| handshake page | CPU 0x1BF000 = `0x86807FEF / 0x7 / 0x2 / 0xABCD` | [V*] |
| boot state / exception mask | CPU 0x118B60 = 1; CPU 0x40008 = 0 | [V*] |
| ready-handshake latency | id1 within 2.0 s; id2 within 20 s (observed ~1.2 s total) | [V*] |
| HCC frame header | 12 B, tag `0x5A5A`, total = payload + 12 | [V] |
| H2D receive slots | 60 (group 0: 41 incl. early 0x2F; group 2: 19) | [V] |
| real H2D messages sent+answered | 2 ordered (context-create, TXQ sync); 1 control stub | [V*] |
| H2D announce consumption | `out[0]` 0x8 -> 0 in 0 polls, `h2d_complete done=1` | [V*] |
| firmware responses | g1 id3 (36 B), g1 id0x12 (40 B) | [V*] |
| cfg80211 bench image | sysupgrade container 6,267,199 B sha256 `22b72852...`; kernel slice 3,372,638 B sha256 `13e81214...`; full-slot hash `1794a531ed19...` | [V] |
| cfg80211 module / clean driver | cfg80211.ko 2,328,548 B sha256 `949393f9...`; hi5622_wifi.ko 505,076 B sha256 `d2b3cf79...`, depends=cfg80211, 0 warnings | [V]/[V*] |
| wiphy channel plan | 2.4 GHz: 13 channels (1-13, 30 dBm); 5 GHz: 19 LU-plan channels (36-48/52-64/100-140, 23 dBm) | [V*] |
| first VAP | wlan0, ifindex 7, type managed, locally-administered documentation address; created and deleted | [V*] |
| calibration exchange (C1) | 298 B request (g0 id2, key 0x0101, cfg id 3507) -> 298 B answer (g1 id3, syn 0x0101) | [V*] |
| MAC host-interface block | CA 0x40040000 (band 0) / 0x40060000 (band 1), slot 5; ready id2 payload seeds the MSDU-info ring pointer table at `0x02060850` | [V]/[V*] |
| data-plane rings | TX: per-TID 8-byte MSDU-info entries + 0x2524 B TXQ table; RX: 256 x 0x63c + 512 x 0xb0 free rings + 4095-entry completion ring, 0x28 B descriptors | [V] |
| calibration boot (C2) | 6/6 messages answered with a 298 B g1 id3; catalogue count 7, `last_total 0x012A`; automated C2 PASS 54/54 | [V*] |
| first real management frames | user_add g0 id7 180 B + user_add_notify g0 id8 180 B + scan_req g0 id0xC 520 B, each consumed in 0 polls; VAP committed `user_id=0 vap_id=0` | [V*] |
| scan attempt | 0 result events (g1 15/16/17, g2 2) within 5000 ms -> `cfg80211_scan_done(aborted=1)`, iw "scan aborted!"; wall = firmware VAP start object | [V*]/[I] |
| scan/provider module | 676,180 B sha256 `4a1c3b9a...`, caps `0x1f` (scan_req+scan_parse+user_add+notify+del), 9 cfg80211 imports, 0 warnings | [V] |
| MAC data-plane host module | 462,952 B sha256 `8fc693ad...`, dp_probe/dp_setup/dp_rings/dp_txq_sync, 0 warnings; automated E1 PASS 37/37 | [V]/[V*] |
| TXQ-table address sync | g0 id1 key `0x0320`, 32-byte frame, 4-byte table device VA; firmware worker CA `0x81445`; accepted (`txq_addr_sent=1 ok=2 timeout=0`) | [V]/[V*] |
| one-command matrix | boot/C2/D1/D2/E1 = PASS/PASS/PASS/BLOCKED-DRIVER/PASS; C2 54/54, D1 21/21, E1 37/37; one reboot observed back in 61.5 s | [V*] |
| H1 read pack | handler-table pointer CPU `0x10C1B0` = `0x00118D68`; live table CPU `0x118D68` all zero at 2 s and 48 s; ROM RAZ `0x2000` = 0; anchor CPU `0x40000` = `0x00046971` | [V*] |
| mode-1 VAP-base provider | debug-stripped module 738,112 B sha256 `e09dc697...` (D2 TRUE PASS build); base provider tree 748,888 B sha256 `b42ff4ec...`; AP-start module 206,708 B sha256 `fd8e1439...` | [V]/[V*] |
| first real scan results | 6 x 124 B `g1 id17` channel reports (channels 1-6) + 6 x 509 B `g1 id15` beacons within the 5 s bound; 12 BSS entries decoded in a 13-channel run (4/13 channels, ~3.5 s/ch) | [V*] |
| AP-start frames | vap_add `g0 id1` 48 B -> vap_start `g0 id1` 40 B -> ap_ssid `g0 id1` key 0x0005 64 B -> ap_cfg `g0 id1` key 0x0127 184 B; consumed in 0 polls each | [V*] |
| AP readback | page `0x86807FEF/7/2/0xABCD`, state 1, mask 0, staging 0; `ctx0+0x18C` changed (`0x70094810`); VAP pointer `ctx0+0x190` = `0x01010F18` (nonzero) | [V*] |
| first live AP | hostapd-mini 2.12, ch 6 OPEN, beacon_int 100, DTIM 2; `wlan0: AP-ENABLED`, `type AP, ssid cudy-ap-test` | [V*] |
| air-TX merged module | 886,864 B sha256 `ca32aec4...`; first TX frame: 32 B probe-req MPDU, ring entry 0, table wptr 0->1, completion_pending=1 = POST-ONLY | [V]/[V*] |
| ABI-correct bench image | container `df6a78c3...` 6,533,439 B; kernel slice `13e81214...` 3,372,638 B; slot `1794a531...`; rootfs UBI `62344e80...`; image apk db kernel `6.18.60~6f995278...-r1`; ABI verifier 25/25 | [V] |
| T0 userspace deploy | 49/49 gates, 0 failed; native `iw` 6.17 + `iwinfo` + `libiwinfo` + `wireless-regdb` persistent in the overlay; modules stay `/tmp` | [V*] |
| hostapd package set | hostapd-mini 283,790 B + hostapd-common 1,922 B + ucode-mod-nl80211 24,228 B + ucode-mod-rtnl 29,292 B; no kernel= dependency; 88/88 host checks + 14/14 selftests | [V] |
| all-PASS matrix evidence | `build/wifi-bench-phase-matrix/20261124T094716Z/` (PHASE-MATRIX.md, per-phase dirs); C2 54/54, D1 21/21, D2 PASS, E1 37/37 | [V*] |
| image flash (2026-11-27 06:30) | ABI-correct container `df6a78c3...` flashed via `stock-telnet-install.sh` (slot read-back `1794a531...`, `ubiformat` OK, active pair untouched, handover `bootflag=b` + `boot_reg=0x21`); box boots 6.18.60 from rootfsb; `apk query kernel` = `6.18.60~6f995278...-r1` = the kmod pin | [V*] |
| rootfs-native path | `modprobe cfg80211` + `modprobe hi5622_wifi` rc=0 from `/lib/modules/6.18.60` (no `/tmp`); native `iw` 6.17 + `iwinfo`; T0 full-set verify 38 gates PASS (one cosmetic receipt-missing: the flash wiped the overlay) | [V*] |
| AP on the flashed image | hostapd AP-ENABLED again, rc=0, four hostapd processes, `type AP, ssid cudy-ap-test`; all four AP-start frames consumed 0 polls; page `0x86807FEF` golden, mask 0 | [V*] |
| AP persistence + new D2H family | live minutes later (hostapd processes alive, same SSID/type, no exceptions); D2H catalogue after the AP start gains `g1 id0x23` (35) count=6 (24 B) + `g1 id3` count=20 (466 B) - absent at boot; meaning [U]/[I] (AP/beacon-period candidate) | [V*] |
| RX live bank | **CA 0x40050000**: +0x00 host-MAC status, +0x08 norm free-ring rptr, +0x0c small rptr, +0x10 completion wptr; config block +0x4c/+0x50/+0x54 (CA 0x40040000) never read by the vendor, stay 0; fired `dp_rx_arm`: status **0x182**, mask **0xfffffcc1**, norm/small rptr **0x20** | [V]/[V*] |
| RX completion path | completion entry = **devVA(buffer+4)**; **36 B** device header at buffer+4; payload len at header+0x1a (= buffer+0x1e; gate len+0x28 <= 0x640), next sub-MSDU at header+0x20 (buffer+0x24), MPDU at buffer+0x28; packed index = 15 index bits + phase bit 15 | [V] |
| WPA2 key protocol | g0 id1 config events: **cmd 0x0078** add_key (body 0x44), **0x0079** set_default_key (0x28), **0x007A** del_key (8 B) -> firmware workers CA **0x0A5825 / 0x0A5C9D / 0x0A4271**; cipher low byte classes data 0x0714 / mgmt 0x3840 / WEP 0x0022 | [V] |
| WPA2 GTK (fired) | `key_add` **96 B**, idx=1, pairwise=0, cipher 0xfac04, key_len=16; `key_default` **68 B**, idx=1, multi=1; both consumed 0 polls; page golden, mask 0 | [V*] |
| five-message AP order | channel config **0x0124 FIRST** (ap_chan 32 B, band=0 channel=6; worker CA 0x841B8) -> SSID 0x0005 -> auth 0x006D (WAL only) -> ap_cfg 0x0127 -> **VAP start 0x0065 after ap_cfg**; fired order reached AP-ENABLED | [V]/[V*] |
| scan ceiling | scan engine consumes **slot 0 only** (obj+0xEC; +0xFC start/+0xFD step/+0xFE end/+0x103 mode/+0x104 counter/+0x106 stride 6); no subtype-1 write to +0xFE; dispatch row CA **0x10CD5C**; vendor shape **44 B** (32 B payload); subtype-2 flag payload+0x14; ceiling **6** ch/request, **7-8** with H1 pacing | [V]/[V*] |
| STA / 5 GHz / HT-VHT | connect WAL cfg-event **0x0123** (108 B body), disconnect **0x00d0** (16 B); firmware owns auth/assoc (connect-SME); vendor bands 2.4 GHz **14** ch / 5 GHz **29** ch; HT cap **0x0062**; VHT 2SS MCS0-9 (**0xfffa**), VHT cap **0x00400000** (2.4) / **0x00400064** (5G, 160 MHz+SGI) | [V] |
| TX completion + restart + soak | TXQ-table publish -> **COMPLETED (MAC)** (device rptr == wptr); E2 plain restart -> AP-ENABLED again in one boot; 40-min soak **242/242 samples golden**; final R4 combined module stripped **269,688 B sha256 b5dfb883...** (unstripped 990,760 B `64ece213...`) | [V]/[V*] |
| static-wave artifacts | rx-static stripped **280,736 B `5b43d0a2...`**; wpa-static **282,048 B `11cee0aa...`**; beacon-order **276,140 B `300cd392...`**; scan-full stripped **263,704 B `4dd008d8...`**; sta-static **278,044 B `333c72a5...`** (all host-built, local evidence only) | [V] |
| final all-patch module | stripped **308,080 B sha256 `5b587949...`**, 47 parameters, 0 warnings, cfg80211 contract 10 (opensource `008aca5`); chain base -> polish `d145a205` -> dp-tuning `175a4617` -> scan R4 -> stability `b5dfb883` -> rx -> beacon order -> wpa -> sta | [V] |
| one-boot final validation | vendor five-message AP order -> hostapd `AP-ENABLED` with WPA2 config; GTK key_add 96 B + set_default 68 B; TX `COMPLETED` (TXQ table wptr=rptr=1); RX arm status 0x182/mask 0xfffffcc1/rptr 0x20; page golden/mask 0 | [V*] |

---

## 2. System map: the endpoint, the host, and the layers between

### 2.1 The two sides

The board has two PCIe root complexes; the Wi-Fi endpoint is a separate die (Hi5622V100) behind
one RC, enumerated as vendor:device `59e7:0005`, class `0x028000` [V*]. Inside the endpoint there
is a dual-Cortex-A9-class CPU complex running a **closed 928,920 B firmware**, a 16 KiB mask boot
ROM, a small WRAM, CPU1 TCM windows, PKTRAM for ring nodes, ACP SRAM for frame bodies, and an IO
block containing the boot gate, the mailbox and the "ETE" DMA engine [V]/[V*].

The host side is ours: an OpenWrt 6.18.60 kernel with an original DWC-based `luofu` PCIe
controller driver (candidate patches 013/013a) plus an original bench-instrument module that
implements the transport, the boot protocol and the HCC service. No vendor host module is loaded
in our stack; the vendor host stack is only ever used as a read-only reference (stock pair A) for
golden-state capture and kprobe instrumentation [V*].

### 2.2 Address spaces and translation

Three address spaces appear constantly and confusing them is the most expensive class of mistake
in this project:

1. **Device CA** - the endpoint's internal address space, reachable through the RC windows.
   Device IO CAs are `0x40000000..0x4011FFFF`; the boot gate and mailbox live there.
2. **Firmware CPU address** - the firmware's flat CPU view (region 5), including its own image at
   CPU `0x40000` (= device CA `0x01240000`), the handshake page at CPU `0x1BF000`, and the
   image-header exception area at CPU `0x40000..0x4004B`.
3. **Host BAR0 offset** - what our driver actually writes through the RC outbound window.

The translation rules (full table in [TABLES.md](TABLES.md) T2):

```
device IO CA  -> host BAR0 :  0x3B8000 + (CA - 0x40000000)      (region 3)
firmware CPU  -> host BAR0 :  0x6B8000 + CPU                    (region 5)
host phys     = 0x40000000 + BAR0_offset                        (endpoint 0, bench)
image file    -> CPU       :  CPU = file + 0x40000
mask ROM      -> CPU       :  CPU = ROM dump offset (ROM mapped at CPU 0)
```

The trap: the two formulas look interchangeable but are not. The inherited transport macro
`HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)` masks the CA to 20 bits, which is only correct
for CAs below `0x40100000`; the D2H ack/re-arm registers at `0x40101438/0x40101414` must map to
BAR0 `0x4B9438/0x4B9414`, not `0x3B9438/0x3B9414`. That 1 MiB error silently hid for a long
time because `out[1]` (CA `0x40039014`) is below the boundary (finding F6) [V*].

The second trap: CPU addresses below the IO block (e.g. `0x40008`, `0x4000C`, `0x1BF000`) are
**firmware CPU** addresses, not device CAs. "mask CA 0x40008" in older notes means CPU
`0x40008`, i.e. BAR0 `0x6F8008`, and the "exception record area CA 0x4000C" is CPU
`0x4000C` = BAR0 `0x6F800C` = phys `0x406F800C`. The device IO gate is a different register at
CA `0x4000010C`. This report always writes "CPU" or "device CA" explicitly.

### 2.3 The stack, layer by layer

```
+---------------------------------------------------------------+
| Wi-Fi userspace (stock OpenWrt): netifd, hostapd/wpa_supplicant, iw  |
+---------------------------------------------------------------+
| cfg80211 full-MAC interface (the 39-op vendor surface)             |
|   M0 wiphy + M1 VAP netdev: on the bench image (hardware-validated)|
|   M2 scan/station stubs: -EOPNOTSUPP; M3+ deferred                 |
+---------------------------------------------------------------+
| HCC message layer: 12-byte header, groups 0/1/2/3/4, mailbox ids  |
|   H2D: SR ring commit -> out[0] mask -> out[2] doorbell -> id3     |
|   D2H: out[1] bitmap -> ack out[3] -> read rings -> re-arm out[4]  |
+---------------------------------------------------------------+
| ETE rings: SR0-2 (host->device) and DR0-3 (device->host)           |
|   node = 8 B {devva, (len<<16)|0x6D2B}; depth 32; packed indices   |
|   engine A->B copy is firmware-triggered; DR direction stays idle  |
|   (direct read of the device group-A rings is our substitute)      |
+---------------------------------------------------------------+
| MAC frame plane: host-interface ring block, slot 5                 |  <- mapped; host setup + TXQ sync fired (E1), no frame yet
|   TX per-TID MSDU-info rings; RX free + completion rings           |
|   (separate from HCC/ETE; seeded by the ready handshake)           |
+---------------------------------------------------------------+
| PCIe transport: BAR0 six windows, iATU viewports, mailbox regs     |
+---------------------------------------------------------------+
| Chip: mask ROM S0/S1 -> closed firmware (CPU0 image + CPU1 TCM)    |
+---------------------------------------------------------------+
```

### 2.4 Text schematic of the data paths

```
HOST (our 6.18 kernel)                 ENDPOINT (Hi5622V100)
+--------------------------+          +--------------------------------------------+
| luofu-pcie RC driver     |          | mask ROM (16 KiB; S0 CPU0 / S1 CPU1)       |
|  - endpoint reset pulse  |  PCIe    | closed firmware (CPU0 entry 0x46970,       |
|  - six inbound windows   |<-------->|   CPU1 entry 0xC742C, boot task 0x47628)   |
|  - BAR0 MMIO             |          | handshake page CPU 0x1BF000                |
|                          |          | boot gate CA 0x40000108/0x4000010C         |
| hi5622_wifi.ko (ours)    |          | mailbox out[0..5]                          |
|  - transport/boot/HCC    |          | ETE engine (channels at 0x4003A000)        |
|  - direct D2H reader <---+--PKTRAM--+ DR0-3 ring nodes CA 0x0106xxxx              |
|  - H2D sender -----------+---ACP----+ frame bodies CA 0x0200xxxx                  |
|  - cfg80211 wiphy/VAP    |          | MAC host-if block CA 0x40040000/0x40060000   |
|  - (data path: NOT impl.)|          |   slot 5: MSDU-info/TXQ/RX rings             |
|                          |          | IO block CA 0x40000000..0x4011FFFF          |
+--------------------------+          +--------------------------------------------+
```

### 2.5 Device-side address map (the parts that matter)

| device range | content |
| --- | --- |
| CPU `0x00000-0x003FFF` | mask boot ROM (readable 16 KiB window) |
| CPU `0x0040000+` / CA `0x01240000` | firmware image (upload target) |
| CPU `0x010938` | ROM status word |
| CPU `0x040008` / `0x04000C` | exception-seen bitmap / first-fault context frame |
| CPU `0x10C0F4` / `0x10C190` / `0x10F1F0` | firmware message object / dispatcher hcc instance / hcc ring records |
| CPU `0x118B60` | boot-task state |
| CPU `0x17D42C` | boot/FIQ slot (value 0x000C19D1 on a golden boot) |
| CPU `0x1B2800` / `0x1B2860` | platform-custom block / wifi-custom block |
| CPU `0x1BF000` | ROM<->image handshake page |
| CA `0x00400000` / `0x00410000` | CPU1 ITCM / DTCM windows |
| CA `0x01000000+` | PKTRAM (device D2H ring nodes) |
| CA `0x02000000+` | ACP SRAM (device D2H frame bodies) |
| CA `0x40000000` | IO block start (boot gate at +`0x108`, mailbox at +`0x39010`, ETE at +`0x3A000`) |
| CA `0x40040000` / `0x40060000` | MAC host-interface ring block, slot 5 of the per-band channel table (band 0 / band 1): MSDU-info ring pointer table (+0x10), RX free/completion rings (+0x20..+0x54), tx_ba_info (+0x00/+0x04) [V] |
| CPU `0x10C23C` / `0x103BF8` | firmware group-0 id-1 config key tables A (217 slots) / B (88 slots), sentinel {0x1451, 0}; the hdpp/alg command namespace [V] |
| CA `0x40100000+` | MAC-side message registers (out[3]/out[4]/D2H doorbell) |

---

## 3. The boot chain

### 3.1 Method and evidence

The boot chain was reconstructed from three independent sources that agree: (a) the 16 KiB mask
ROM dump, disassembled by `wifi-rom-dump/tools/romdis.py` (86/86 checks) [V]; (b) the firmware
image, disassembled by the fw-postboot/rom-flow/ete-engine lanes [V]; (c) bench measurements of
the handshake page, state and exception mask across GP1-GP10 and the PD3/PD4 sessions [V*]. The
ROM is a real mask ROM: it contains 2,073 bytes of code/data and no hidden body - the readable
window **is** the whole relevant ROM, which is what made the root cause findable at all [V].

### 3.2 S0: reset and the CPU0 primary path

At the reset vector the ROM sets up caches/MPU off, per-mode stacks, an ARMv7-R-style MPU region
map (whole-space default, 4 MB IO at `0x40000000` XN/device, 2 GB RAM window), then splits on
`MPIDR & 3` into a primary (CPU0) and a secondary (CPU1) path [V].

The CPU0 path (`0x50C`/`0x54C`) is a cold fallback with one host dependency:

1. clear the stale image header entry-A pointer (store 0 to CPU `0x40000`);
2. write `0xBAAD` to the boot gate CA `0x4000010C`;
3. run the analog/PHY bring-up and wait for the AFE;
4. **poll CA `0x40000108` until it equals `0x00005A5A`** - this is the host release;
5. write `0xCEED` to CA `0x4000010C` (16-bit), set handshake page bit8;
6. re-read the image header entry A (CPU `0x46970`) and `blx` it.

The consequence for the host is absolute: **the firmware must be uploaded before the release
write**, because the ROM re-reads the entry pointer the host firmware provides [V].

### 3.3 S1: CPU1 and the handshake ACK

CPU1 runs the secondary ROM path ROM `0x564`. It clears its own handshake words, arms, then
waits (WFI loop) until **both**:

* handshake page word `+C` (CPU `0x1BF00C`) equals `0x0000ABCD` - written by image entry A, and
* the boot gate CA `0x4000010C` reads `0x0000CEED` - written by the CPU0 ROM path after the
  release.

Only then it executes `strd r1,r0,[r3,#4]` with `r0 = 1 << (MPIDR & 3)`, i.e. **writes
handshake page `+8 = 2`** at ROM `0x5FE`, sets page `+4 |= 4`, and `blx`-es image entry B
(CPU `0xC742C`) [V]. The image never writes 2 - it only consumes the ACK [V]. The golden
handshake page is therefore a three-party artefact: image entry A wrote `0xABCD`, the ROM wrote
`0xCEED` and `2`, and the host's only job was to not disturb the gate.

### 3.4 The handshake page contract

```
CPU 0x1BF000  (BAR0 0x877000, phys 0x40877000)
  +0x00  0x86807FEF   progress bitmap; bit14 set only after +8 == 2
  +0x04  0x00000007   vendor golden value
  +0x08  0x00000002   S1 ACK written by mask ROM CPU1 only (1 << core)
  +0x0C  0x0000ABCD   request written by image entry A
  +0x10  changes       non-contract value (stock read 0xE2F16F38; ours 0xE0F16F38)
```

Additional boot-state observables that form the golden gate: state CPU `0x118B60 == 1` (the boot
task ran) and exception-seen bitmap CPU `0x40008 == 0` (no exception was taken) [V*].

### 3.5 The critical gate: one 16-bit register, two handshakes

This is the project's single most important finding (F1). CA `0x4000010C` is used **twice** with
mutually exclusive values:

* the **ROM boot stage** needs `0xCEED` there to survive until CPU1's S1 check passes;
* the **image service/exception handlers** zero that register when entered and then spin waiting
  for `0x0000CECE` (image handler code at CPU `0xC20F8`/`0xC6E14`).

Our original transport, believing the announce path required it, wrote `0xCECE` to this register
during boot and kept re-writing it. That destroys the ROM's `0xCEED`, so CPU1's S1 check can
never pass, the ACK `+8 = 2` is never written, entry B never runs and the boot task never starts.
The vendor host **never writes `0xCECE`** (independent scan of all 60+ vendor modules: 0 hits)
and its boot has mask 0, so `0xCEED` survives [V]/[V*].

The corrected host protocol is subtractive, which is why it took a ROM dump to find:

1. upload the image and pre-seed the custom blocks;
2. write `0x00005A5A` to CA `0x40000108` (release) - **only** that;
3. wait for handshake page `+8 == 2` (latch once; never re-gate on a live page read, because an
   exception path may mutate the page);
4. only then, if an image handler is observed spinning, service `0xCECE` as a bounded,
   evidence-driven acknowledgement.

Finding F1 has a corollary that is itself a correction: the "S1 is ROM/SoC-internal and not
host-triggerable" conclusion reached at 2026-11-24 08:45 was wrong - the missing ingredient was
removing our own interfering write.

### 3.6 Host-side pre-release obligations

The kprobe capture of the working vendor stack (2026-11-24 07:20) gives the exact pre-release
sequence the host must reproduce [V*]:

```
firmware_download()
  bal_write(dst=CA 0x1240000, data=firmware chunks)      -> our: one contiguous upload + verify
  dev_custom_send / bal_write(dst=CA 0x1B2800)            -> platform-custom block, 24 words
  bal_write(dst=CA 0x1B2860)                              -> wifi-custom block, 618 words
  bal_write(dst=CA 0x40000108, value=0x5A5A)              -> release
```

Also required: ETE rings armed and D2H service running **before** the release (the vendor does
this) so the ready handshake cannot be missed [V]. Our bench ladder does exactly that
([TABLES.md](TABLES.md) T19).

The custom blocks matter: without the platform block entry A cannot copy the CPU1 ITCM/DTCM
payloads (the four section bounds at `+4..+0x10`), and without the TCM payloads CPU1's S1 path
cannot run. GP3-GP5 established this experimentally: with the four bounds only, the TCM loaded but
S1 still did not run; with the full 24-word block the TCM became byte-identical to the vendor
(including the coreword `0x37C8F8D3`) - and S1 *still* did not run, because of F1 [V*].

### 3.7 Post-S1: image execution and the golden state

After S1 the picture is exactly the vendor's:

| observable | value | meaning |
| --- | --- | --- |
| page | `0x86807FEF / 7 / 2 / 0xABCD` | handshake complete |
| state CPU `0x118B60` | 1 | the boot task ran |
| mask CPU `0x40008` | 0 | no exception |
| TCM windows | `0xE59FF018 / 0x40034000 / 0x37C8F8D3` | CPU1 payloads byte-identical to vendor |
| boot/FIQ slot CPU `0x17D42C` | `0x000C19D1` | boot machinery armed |
| `out[1]` | `0x40` initially, then D2H traffic | firmware deposits/stages messages |

The firmware then posts its ready handshake: `g4 id1` (device plat ready) and `g4 id2`
(DEVICE READY) with the 4-byte payload `0x01060850` - a device-RAM address the vendor host stores
in its per-core read-message record [V*]. On our boot both arrive well inside the vendor bounds
(id1 within 2.0 s, id2 within 20 s; observed total ~1.2 s) [V*].

### 3.8 The layer that stays dark, and why we can still work

Even with a golden boot, the firmware's own IRQ/thread layer does **not** become active on our
boot. The evidence: the event-controller enable and scheduler-timer reads, when correctly mapped,
show the scheduler was armed (an earlier "fingerprint absent" reading was an instrument aliasing
bug - see corrections C7), but the firmware's mailbox consumer never acks because **event 0x4C is
never delivered** to the image dispatcher. No H2D message can create that event: the vendor host
sends nothing between release and ready, and no H2D handler reaches the dispatcher (the
`wifi-hcc-link` and `wifi-fw-postboot` lanes closed that negative) [V]/[V*].

A later experiment closed the remaining host-side question about the receive engine: the DR
(D2H) A->B copy has exactly one host-owned state delta (the group-B published index `+0x38`),
and writing it to the current fill (E1a) or to zero (E1b) on a golden boot moved nothing - no
status bits, no trigger counters, no ring fills (section 6.5). The only start condition is
device/ROM-authored engine state, so the direct device-ring read is not a stop-gap but the
supported D2H receive path for this firmware [V*]/[I].

Two host-side answers were developed instead of fighting the closed IRQ layer:

* **receive** (D2H): read the device's own group-A rings and frame bodies directly through the
  BAR0 windows - the frames are staged by the firmware regardless of the engine (finding F4);
* **send** (H2D): PD3/PD4, a firmware patch that calls the firmware's own H2D dispatcher wrapper
  from the boot-task epilogue, gated by an `out[0]` poll (finding F3). This is a **research
  instrument**, not a product change.

### 3.9 Boot decision ladder (text form)

```
power-on
  |
  +-- vendor stack (stock boot): GPIO reset pulse -> ROM S0 -> CPU0 cold fallback
  |     -> waits 0x5A5A -> writes 0xCEED -> entry A -> page +C = 0xABCD
  |     -> CPU1 S1: page +C == 0xABCD && gate == 0xCEED -> page +8 = 2, entry B
  |     -> boot task -> ready handshake -> working Wi-Fi stack
  |
  +-- our transport, BEFORE the fix: pulse -> ROM S0 -> upload -> release -> **0xCECE writes**
        -> 0xCEED destroyed -> CPU1 S1 loops forever -> page +8 = 0
        -> (GP1 flaky "successes" were stale vendor WRAM custom blocks from earlier stock boots)
  |
  +-- our transport, AFTER the fix (GP8+): pulse -> ROM S0 -> upload + custom blocks
        -> release 0x5A5A ONLY -> page +8 = 2 -> golden state -> ready pair
        -> [PD3/PD4 patch for the mailbox consumer] -> H2D messages answered
```

---

## 4. Critical findings

Ten findings carry the investigation. F1-F6 are the boot/protocol findings of v1; F7-F10 are the
management/data-plane findings integrated in v2. Each is stated as symptom -> root cause ->
evidence -> fix -> residual risk, so that a future reader can tell what is settled and what is
not.

### F1. The `0xCEED` boot handshake was being clobbered by our own `0xCECE` writes

**Symptom.** The chip would reach a "pre-S1" state (page `0x86803FEF / +8 = 0 / 0xABCD`, state 0,
mask 0) or, in other sessions, a RAM-fallback page; the CPU1 ACK `+8 = 2` never appeared, entry B
never ran, the boot task never started. GP1 sometimes worked (2/4 boots), GP2-GP6 never did.

**Root cause.** CA `0x4000010C` is a dual-use 16-bit register (section 3.5). Our transport wrote
`0xCECE` during boot (meant as the image's announce gate), overwriting the ROM's `0xCEED`. CPU1's
S1 WFI loop requires `0xCEED`; it never passes; no ACK is written.

**Evidence.** ROM disassembly at ROM `0x506` (CPU0 writes `0xCEED`) and ROM `0x5FE` (CPU1
writes `1<<core`), image handler code at `0xC20F8`/`0xC6E14` spinning for `0xCECE`, the
subtractive experiment GP8 (release-only) producing the exact vendor golden page, and the
pre-fix GP2-GP6 differential [V]/[V*]. The GP1 flakiness is explained: the successful runs
consumed a stale-but-valid vendor platform block left in chip WRAM by earlier stock boots [I].

**Fix.** Release-only boot: write `0x00005A5A` to CA `0x40000108` and nothing else during boot;
service `0xCECE` only after the handshake is latched, and only in response to observed evidence
(`out[1]` bit2 / mask change). The clean driver encodes this as a latched boot gate and a bounded
evidence-only CECE policy [V]*.

**Residual risk.** The image's real CECE service role (what exactly the vendor's vector handler
expects) is only partly understood; our successful ordered lap wrote no CECE at all, and a later
flow may need the exact vendor ordering.

### F2. The exception-staging overlap: a "safe" firmware code cave was not safe

**Symptom.** The PD3 patch (stub in the image-header exception-record area CPU `0x4000C`) worked
for the first mailbox message but the first-fault record was unreadable - it contained a *later*
exception frame, and the fault looked like "the mailbox consumer died" even though it had worked.

**Root cause.** The image's exception-staging routine writes its 17-word frame **exactly over**
CPU `0x4000C..0x4004B` - the same bytes the PD3 stub occupied. After the first (data-abort)
exception the poller's code was corrupted; executing garbage produced a secondary undef exception;
the staging area keeps only the last frame, so the true first-fault PC was lost.

**Evidence.** PD3 bench (2026-11-24 22:10) consumed the announce; the next session (2026-11-25
00:20) read the exception record after a TXQ-sync fault and found the 17-word frame with
`pc = 0x00171E90` (WRAM data, not code), `sp = 0x001B9FC8`, `lr = 0x00041E4F`, and values
`0x00081039`/`0x00043DF5` pointing into the g0 id1 handler path. PD4 relocated the identical stub
to CPU `0x108240`, inside a 637-byte all-zero static-data alignment gap with zero branch/literal
references, outside both boot memsets and the staging area; after firing, the cave bytes were
still present, proving the relocations [V*].

**Fix.** PD4 stub home; the PD3 cave is deprecated for first-fault analysis (it remains
bench-validated for mailbox consumption). The corrected patch-entry bytes and image hash are in
[TABLES.md](TABLES.md) T15.

**Residual risk.** Even with PD4 the firmware's exception path cascades dabt -> undef, so the
first-fault frame is still not directly readable in the PD4 confirmation session; the current
best evidence for the g0 id1 fault is the static handler analysis plus the pd4 lane's
field-B context proof.

### F3. Bypassing the dead IRQ layer with the firmware's own dispatcher (PD3/PD4)

**Symptom.** With a golden boot, host->device frames reached the device ring but the firmware never
acked the mailbox: `out[0]` stayed `0x8`, no `out[1]` bit2, `h2d_timeout` lane closed. The
firmware's H2D dispatcher (CPU `0xC18AC`, wrapper `0x40294`) is only reached from the image
vector handler `0x86E14` on event 0x4C - and that event is never delivered on our boot [V].

**Root cause.** The firmware's IRQ-unmask/thread layer never runs on our boot (section 3.8). This
is not a host-fixable register: the event controller, GIC and scheduler are firmware/ROM-internal
and the vendor sends nothing pre-ready that could kick them.

**Fix (research).** PD3 redirects the boot-task epilogue (file `0x916A`, CPU `0x4916A`) into a
64-byte guarded stub that polls `out[0]` and, only when pending, calls the firmware's own
dispatcher wrapper `0x40294` with the correct hcc instance context; PD4 moved the stub to the
truly-safe cave. On the bench PD3/PD4 consumed every announce in 0 polls (`h2d_complete done=1`,
`h2d{pub=... done=1}`) without breaking the golden page, and the ordered lap then ran with mask
staying 0 [V*].

**Evidence.** Bench sessions 2026-11-24 22:10 (PD3, announce consumed), 2026-11-25 01:10 (g0 id0
control stub consumed, no exception), 02:10 (PD4 confirmation), 05:10 (ordered lap, both messages
consumed, firmware responds) [V*]. Dispatcher contract from PD2 analysis: `r0` = hcc instance
CPU `0x10C190`; fields `+4` = `out[0]` CA `0x40039010`, `+0xC` = firmware ack CA
`0x400392F0`, `+0x10` = doorbell CA `0x400392D4`, `+0x20` = handler table; preserves
`r3-r7`; no per-core context needed [V].

**Residual risk.** This is a patched vendor image - a bench instrument only. It proves the protocol
and gives us a send path; it is not a route to a shipped product, and it does not revive the rest
of the firmware's thread/scheduler services (the ETE copy trigger and any timer-driven flow remain
dark).

### F4. Direct device-ring read: D2H without the ETE engine

**Symptom.** The device's D2H pipeline stages frames, but the ETE engine that should mirror them
into host DRAM stays idle (`status CA 0x4003A874 = 0`, send words 0, fills static). Earlier work
concluded the D2H service could not be entered without waking the engine.

**Root cause / discovery.** The firmware is not required to use the engine for us to read its
output: it stages 8-byte nodes in the **device** group-A DR rings (PKTRAM, CA `0x01060110` /
`0x01060440`) and the frame bodies in device ACP SRAM (CA `0x02000000+`). Both are inside BAR0
regions the host already owns. The engine's A->B copy is firmware-triggered (the only kick is the
sending trigger CPU `0xC625A` via event 0x2E, itself fed by device-authored status) and no host
write can start it [V]/[I].

**Fix.** A direct-read D2H receive service (`wifi-d2h-direct`): poll the device group-A rings
through BAR0, decode the 12-byte header, validate `total == node len`, catalog by (group,id),
deliver the ready pair, EXCP_INFO, heartbeats, temp-state frames. Consumer advance: the device
`+0x1C` is engine-only; the host-owned `+0x38` republish is the only vendor-analog index write
[V]*.

**Evidence.** Bench 2026-11-24 13:10 (frames decoded from the staged rings) and 14:00 (complete
ready handshake: id1 + id2 payload `0x01060850`, 21 frames, overruns 0) [V*].

**Residual risk.** The direct-read path is a research instrument: it depends on layout constants of
this firmware, and the engine's copy (if it ever runs) would be the vendor's actual mechanism. It
does not resolve why the trigger never fires. The later ETE-wake ladder (E1a/E1b, section 6.5)
measured the only host-owned candidate negative, so "why" is now classified as device/ROM state
rather than a missing host write.

### F5. The context-create precondition and the ordered operational loop

**Symptom.** Sending the vendor's first post-ready frame (TXQ config sync, `g0 id1` cmd
`0xD8`) as the first real H2D message faulted the firmware (`mask 0x12`, exception frame
`pc` in WRAM data). A payload-free `g0 id0` control stub was consumed cleanly, proving the
transport.

**Root cause.** The `g0 id1` key-`0xD8` handler resolves `hdr[3]` as a module index and
dereferences the module's context buffer at `ctx+0x190` (worker CPU `0x81039`/`0x43DF5`).
Only the context-create helper (`CA 0xFE6BC`) writes that field, and on our boot it is NULL:
the boot's own `0xFE6BC` call creates module 18 with type 0 and takes the no-alloc path. The
H2D route to `+0x190` is a **config event with key `0x0064`** (group 0 / id 1), whose data
block selects the module index (`data[4] = 4`) and the create type (`data[0] in {1,2,3}`).
Type 3 took the no-alloc branch on the bench; **type 1 allocates** [V*].

**Fix.** An ordered lap: context-create type 1 -> read-only pack -> TXQ config sync. On the bench
the type-1 create wrote `ctx+0x190 = 0x01011448`, `ctx[0] = 4`, `ctx+0x10 = 0x00010001`,
`ctx+0x434 = 0x0100478C`; the following TXQ sync was consumed in 0 polls with **mask 0 and no
staging frame**, and the firmware answered with `g1 id3` and `g1 id0x12` [V*].

**Residual risk.** The key-0x64 frame is **synthesized from the firmware parser, not captured
from the vendor driver**; the vendor's sender for this key is unproven [I]. The two response frames
are observed but only partially decoded. The type-1/2/3 dispatch is inferred from the shared
allocator analysis [V]/[I].

### F6. The region-3 translation bug (hidden 1 MiB error)

**Symptom.** The host's D2H ack/re-arm writes appeared to have no effect; the firmware's deferred
notify words `g+0xB4`/`g+0xB8` stayed at `0x40` for dozens of sessions; the image's flush never
retired.

**Root cause.** The inherited macro `HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)` truncates
device CAs to 20 bits. For region-3 CAs >= `0x40100000` (out[3] `CA 0x40101438`, out[4]
`CA 0x40101414`) it produces BAR0 `0x3B9438/0x3B9414` instead of the correct
`0x4B9438/0x4B9414` - writes landed in unrelated registers. `out[1]` is below the boundary,
which is why the bug survived.

**Fix.** Real region-3 translation in the clean driver, with compile-time asserts for every
out/mailbox/ETE CA and a selftest mirror [V]*.

**Evidence.** Bench 2026-11-24 21:10: after the fix, `g+0xB4 = 0` and `g+0xB8 = 0` - the
deferred-notify flush retired for the first time; F4 verified on hardware [V*].

**Residual risk.** Other inherited fragments may still use the 20-bit macro; the clean driver is
the single source of truth and the bench instrument modules are deprecated.

### F7. The first calibration exchange answers on a generic config-sync channel

**Symptom / context.** After the ordered message lap the vendor's next boot step is the factory
calibration: six `alg set_*` config events, HCC group 0 id 2, key `0x0101`, each carrying a
270-byte alg_cfg blob (298 B frame, node length = declared total). The question was whether our
host can drive that family and whether the firmware answers.

**Design (from the firmware parser, not a vendor-driver capture).** The firmware's key-0x0101
handler clears the request's `dev_ret`, reads `need_w4` and answers through the single config-sync
sender with `syn_id 0x0101`, `body_len 270`, the request cfg id echoed and `dev_ret 0`. The six
vendor cfg ids and value layouts were recovered statically: 3502/3503 (2.4 GHz power, 104 B),
3504/3505 (5 GHz power, 70 B), 3506 (xo ppm, u32) and 3507 (rssi, 40 B) [V].

**Bench (ladder C1, 2026-11-25 12:30).** After the type-1 context-create and the TXQ config sync,
one zero-valued `set_rssi_param` (3507, field B = 4) was consumed in 0 polls; the direct D2H
catalogue then showed a second g1 id3 frame with `last_total = 0x012A` (298 B) - exactly the
mirror length of our 298 B request. Boot page and state stayed golden, mask 0, no exception [V*].

**C2 (2026-11-25 16:30; automated 23:40).** The full vendor-order six-message calibration boot
(cfg ids 3502-3507, `h2d_send_budget=8`) ran on a fresh PD4 golden boot: every write was consumed
in 0 polls (`h2d_status ok=8 timeout=0 refused=0`) and the D2H catalogue showed **six 298 B g1 id3
answers** (count 7 = one unsolicited frame + six answers, `last_total 0x012A`, `last_len 298`);
page/state/mask clean, `bad_writes=0`. The same sequence then re-ran under the automated phase
runner as phase C2: **PASS, 54/54 checks, 0 failed, 28 steps, 7.02 s** (2026-11-25 23:40) - the
first fully automated bench-phase pass [V*].

**Residual risk.** The answer bodies were recycled by the driver/firmware before the read-only
capture, so the evidence for C1/C2 is count + exact-length, not the decoded body. g1 id3 is **one
generic config-sync channel** dispatched by `syn_id` (`0x0101` alg, `0x0102` cali, `0x07xx`
per-VAP), not a per-message answer type (correction C20). An immediate post-fire read (or a driver
logging extension) can still capture one answer body.

### F8. The first real Wi-Fi interface on our own image (cfg80211 M0/M1)

**Symptom / context.** Our 6.18 image had no `CONFIG_CFG80211`, so no Wi-Fi interface could
exist even with a working HCC protocol.

**Work.** Added `CONFIG_CFG80211=m` to the board kernel config, built `cfg80211.ko` and a
cfg80211-enabled variant of the clean driver (one wiphy per PCI endpoint, RE-derived 2.4/5 GHz
band and channel capability, standard ciphers, AP+STA+monitor), then built the release-line image,
extracted the kernel slice and flashed only **kernelb** from the stock pair (full-slot hash
verified). The wiphy is registered from a workqueue **only after** the ready handshake (`g4 id1`
+ `g4 id2`) has been decoded; the cfg80211 layer performs no MMIO and sends no H2D frame [V].

**Bench (2026-11-25 15:00).** The box booted the new kernel (GP1 in-probe endpoint pulse visible
in dmesg), the golden boot and ready pair were reproduced, then `cfg80211: wiphy registered after
the ready handshake` appeared; `/sys/class/ieee80211/phy0` exists and the stock `iw` enumerates
`Wiphy phy0`. `iw phy phy0 interface add wlan0 type managed` created a real netdev (ifindex 7,
locally-administered documentation address, type managed) which was deleted again - **M0/M1 are
hardware-validated on our own image with the vendor firmware running golden** [V*].

**Residual risk.** This was the *management* interface at the v2 cut-off: no firmware VAP
object existed then, scan and station ops were honest `-EOPNOTSUPP` stubs, and no data path was
attached. Later sessions sent the real VAP messages and the scan request (F11), so the
mailbox-layer VAP is now created, but the per-radio firmware VAP start object still does not
exist, no scan result has arrived, and no data path is attached to the netdev. The cfg80211 kernel
config is a **bench-only addition**, not part of the upstream luofu patch series (section 11).

### F9. The Wi-Fi frame plane is a separate MAC host-interface ring block

**Symptom / context.** The v1 model treated the ETE engine's idle D2H copy as a possible data-path
blocker, because it was not clear whether Wi-Fi frames ride the HCC/ETE message plane.

**Finding.** There are two data planes. The HCC message plane (12-byte header on the ETE SR/DR
rings) carries only control and event messages. User/AP/STA frames use the **MAC host-interface
ring block at device CA `0x40040000` (band 0) / `0x40060000` (band 1)** - slot 5 of the vendor's
per-band channel table - a distinct block from the ETE register file, with no cross-writes between
the two modules [V].

The ready handshake seeds it: the `g4 id2` payload `0x01060850` is the device-RAM address of the
MSDU-info ring pointer table, ACP-mapped to `0x02060850` and written to MAC slot-5 `+0x10` [V]/[V*].
The vendor's own next frame is the **TXQ-table address sync** (`g0 id1`, key `0x0320`, 4-byte
device VA). TX uses per-TID 8-byte MSDU-info entries; RX uses 256 x `0x63c` + 512 x `0xb0` free
rings plus a 4095-entry completion ring with 0x28-byte device descriptors; the 217/88-slot key
tables A/B are the g0 id-1 command namespace [V]. Details in section 6.4 and table T24.

**Residual risk.** The architecture is now *host-implemented and bench-fired*, not yet
frame-moving: the dp-bringup lane's `dp_probe`/`dp_setup`/`dp_rings`/`dp_txq_sync` extension
programs the slot-5 block and sends the TXQ-table address sync, and the automated E1 phase passed
37/37 (section 6.8, T26); but no TX ring entry has carried a frame, the RX completion ring and
descriptor bits are unconsumed [U], and the first rings assume user index 0 (no user has queued a
frame yet). The direct-read constraint is an ETE-message constraint only and does not block MAC
DMA [I, supported by the clean E1 run].

### F10. The PD4 research patch: necessity, minimality and the audit boundary

**Symptom / context.** The firmware's H2D mailbox consumer is reached only through the image's
event-0x4C dispatcher path. Event 0x4C cannot be raised from the host on this board (the
event-controller/GIC window at CA `0x40160000` is not host-visible), and the image never polls
the mailbox itself, so an unpatched vendor firmware cannot service any host frame [V]/[V*].

**Patch.** PD4 redirects the boot-task success epilogue (file `0x0916A`, 4 bytes) into a 64-byte
guarded out[0] poll stub in a genuinely dead static-data gap at CPU `0x108240`; when the
dispatcher's handler table is still the static sentinel, the stub substitutes a read-as-zero
table for a consume-only pass. The patched image differs from the pinned vendor blob in exactly
two windows (928,920 B length unchanged) [V].

**Audit (delivered by the fw-audit lane, shared task `task-115`; `wifi-fw-audit/REPORT.md`,
harness `verify_pd4.py` 31 checks + 14/14 selftest, `pd4_variants.py` 14/14).** The published
image rebuilds byte-identically from the pinned blob with exactly two segments (4-byte branch at
file `0x0916A` + 64-byte stub at file `0x0C8240`): 68 patched bytes, 59 differing byte positions,
length unchanged 928,920 B, no other byte differs [V]. The cave proof was independently re-run: the
637-byte gap CPU `0x108197..0x108413` is all zero and bracketed by two referenced read-only
tables, outside both boot memsets and the exception-staging frame, and an exhaustive Thumb census
finds zero branch/literal/ADR references into it [V]. The stub only reads mailbox out[0]
(CA `0x40039010`) and calls the firmware's own wrapper CPU `0x40294` -> dispatcher CPU `0xC18AC`
(which performs ack `out[5]=1`, clear out[0], doorbell `out[2]=8`, then handler dispatch); every
device write is the firmware's, and the host "never write CA `0x400392F0`" rule is unchanged [V].
On the bench the cave bytes survived, the g0-id1 fault reproduced and the ordered lap consumed
every announce in 0 polls with mask 0 and the golden page intact [V*].

**Residual risk / honest limits.** The audit's one material open question was **H1/H2** - whether
the stub's static-sentinel compare always takes the consume-only branch. **H1 is now confirmed** by
the decisive five-word read-only pack, taken on a PD4 golden boot 2 s and 48 s after release
(2026-11-25 21:30; no fire): the handler-table pointer CPU `0x10C1B0` is `0x00118D68` (the
image's static value; file `0xCC1B0` contains it exactly once), the live table at CPU `0x118D68`
is **all zero** at both reads, the ROM RAZ window `0x2000` is 0, and the sanity anchor CPU
`0x40000` is `0x00046971` (image loaded correctly). Therefore the firmware's mailbox dispatcher
handlers **never execute under PD4**; the duplex lap, the calibration answers and the real VAP
frames are delivered through the **non-mailbox ETE/polled H2D path**, and the mailbox
ack/clear/doorbell side effects are bookkeeping [V*]/[I]. This explains why `PD4-ALWAYS0` should
behave identically and refutes the earlier lane assumption that PD4 "dispatches through the real
vendor handler table" (correction C29); `PD4-NOSENT` would enter the empty table and is not a
candidate diagnostic. No vendor-clean
(unpatched-firmware) mailbox-delivery path has been demonstrated: the vendor mailbox sequence is
already reproduced, the deferred-send variant's extra `out[5]=8` pre-kick had been tried once in
the takeover era (accepted, no frame accept), ETE 0x2D/0x2E do not deliver, the ROM has no mailbox
service and the image has no other out[0] reader [V]/[V*]. The one host-only candidate is a spare
inbound iATU viewport onto CA `0x40160000` plus a one-shot raise of event 0x4C (CA
`0x40161188 = 0x1000`, mirror `0x40161288`; enable already set at CA `0x40161108` bit 12) - a new
device-write class, never attempted, needing its own attended design and IRQ-storm bounds
[V]/[I]/[U]. Verdict: firmware patching is required for the closed vendor blob **as delivered**
(conditional on that aperture experiment), not fundamental to the hardware; PD4 stays a
research-only artifact that must never ship [I high].

### F11. The first real firmware management frames and the VAP-start wall

**Symptom / context.** With the wiphy/VAP netdev model working (F8) and the calibration boot
complete (F7/C2), the remaining question was whether the host can build the vendor's VAP-create and
scan-request messages well enough for the closed firmware to accept them. The payload structs were
the report's biggest open wall in v2 (`g0 id7/8` and `g0 id0xC` marked [U]).

**Work.** The vap-scan lane recovered byte-exact builders from the vendor firmware handlers [V]:
`hmac_user_add` g0 id7 (168 B payload / 180 B wire; fixed offsets uid, MAC, type, rssi; handler
CPU `0x9832C`), `hmac_user_add_notify_alg` g0 id8 (168 B / 180 B; uid, alg, 24 B VHT + 44 B HT +
58 B HE capability blocks, nss), `hmac_del_user_notify_dmac` g0 id9 (12 B / 24 B, matching the
shipped del_user attr) and `hmac_enc_scan_send_req` g0 id0xC (508 B payload / 520 B wire: flags,
BSSID, 8 x 36 B SSID slots, 42 x 4 B channel entries, count, dwell 60, type). The scan-ap lane
wired them into the cfg80211 module as provider `vapscan-0e4ad45` with caps `0x1f`
(scan_req+scan_parse+user_add+user_add_notify+user_del), fail-closed behind `allow_frames`
(default 0), plus a bounded between-frames wait (`frame_gap_ms` default 20 ms) because the
firmware clears its mailbox announce on its own service tick [V]. The rebuilt module is **676,180 B
sha256 `4a1c3b9a...`**, 0 compiler/modpost warnings, host selftests **248/248** (+ inherited
454/454 + 242/242 + static 294/294) [V]. The payload wording above is firmware-derived and
structural: the acceptance test is the firmware's own handler, not a vendor capture [V]/[I].

**Bench (2026-11-26 04:30 and 06:30).** First attempt: after boot/ready and a partial C2 ladder, the
provider posted **`user_add` g0 id7 180 B** ("frame post user_add: g0 id7 total 180 B in SR0 slot
2 devva 0x8417c000 producer 0x2 -> 0x3, coalesced announce consumed after 0 polls; posted 1/8"),
and the firmware committed the VAP (dmesg "VAP wlan0 firmware user_add committed (g0 id7,
user_id=0, vap_id=0)"). Two gaps stopped the v2-era module there: the follow-up notify was refused
while the announce was still in flight, and `.scan` refused because the provider registered caps
`0x1d` while the cfg80211 scan op needs `0x3`. After the scan-ap fixes (caps `0x1f`, bounded
announce wait, frame gap) the second session posted **all three frames with zero exceptions**:
`user_add` g0 id7 180 B, `user_add_notify` g0 id8 180 B (posted 2/8), then **`scan_req` g0 id0xC
520 B** in SR0 slot 4 devva 0x8371f000, producer 4 -> 5, **consumed in 0 polls**, posted 3/8. The
driver then waited the bounded 5000 ms for D2H scan events (`g1 15/16/17`, `g2 2`), received
none, and reported `scan DONE (finish-abort, results=0 ... timeout=1) ->
cfg80211_scan_done(aborted=1)`; `iw` printed "scan aborted!". The boot page/state/mask stayed
golden, staging 0, heartbeats continued, no exception, and the direct-read catalogue held only the
boot catalogue (the 36 B unsolicited `g1 id3`) [V*].

**Interpretation.** The complete host chain is proven for the VAP-create and scan-request classes:
builder -> fail-closed gate -> SR node -> mailbox announce -> firmware consumption, extending the
message classes of F5/F7. The scan **response** is missing for a structural reason: the firmware
scan handler (CA `0x6494C`) looks up VAP id 0 and needs the per-radio VAP object in the **started**
state; our g0 id7/id8 only register the user/VAP in the mailbox layer. The per-radio VAP object
create/start family is still [U] (the vendor order is user/vap add -> (start) -> scan/AP; candidates
are the WAL group-2 AP start and the `g0 0xF/0x10` start_sta family) [V]/[U]. The wall has moved
from "can we build the payloads?" to "what starts the VAP?".

**Residual risk.** The builder bytes are [V]/[I] vendor-derived and only firmware acceptance makes
them real; fields the lane could not pin (five scan flag bytes, the channel-index convention) stay
[U]/[I] in T27. A second scan attempt without the VAP start object has no value and was not
retried. No scan result, association or data frame has been observed yet.

**v4 note.** The wall above was lifted one day later: the mode-1 VAP base created the firmware VAP
object and the firmware produced real scan results (section 6.9, correction C34). The AP start and
the first live AP followed from the same precondition (F12, F13, sections 6.10-6.11).

---

### F12. The AP-start crash: a firmware VAP object the AP path never created

**Symptom / context.** After the hostapd capability gates were fixed (F13), the 2026-11-27 03:30
fire reached the driver's `.start_ap`: dmesg `start_ap reached (iftype=3 beacon_interval=100
dtim=2 head=63 tail=9)`, then `ap_ssid` (`g0 id1`, 64 B) posted and consumed in 0 polls. The very
next frame's announce was not consumed (`out[0]=0x8` after 40 polls), the core closed the H2D lane,
and the chip entered an **exception state**: page lost (`0xe22a0c00`), mask `0x18`, staging
`0xffffffff`. The v6 dmesg label called that second frame `ap_auth`.

**Work.** Two corrections came from static re-check of both carriers [V]. First, the g0 config-event
carrier carries exactly two AP members - `ap_ssid` (`g0 id1` key `0x0005`, 64 B) and `ap_cfg`
(`g0 id1` key `0x0127`, 184 B); firmware config-key table A has **no `0x006D` row**, and the
36 B auth command `0x006D` exists only on the WAL group-2 vendor envelope. The label array was
index-based, so index 1 printed `ap_auth` while the provider actually built `ap_cfg` (the 184 B
frame). Second, the decoded AP workers - set-SSID CPU `0x82878` and AP-config CPU `0x84DA4` - both
dereference the firmware VAP object at `module_ctx+0x190` with **no NULL check**
(`ldr r3,[r4,#0x190]; ldrb r2,[r3,#0x64]`). The mode-1 VAP base that creates that object had only
ever been posted by the scan path (`vap_start_first`); the AP path posted AP frames into a context
with no VAP object. The exception is a firmware data abort in the worker, not a bad frame byte.

**Interpretation.** The hostapd -> cfg80211 -> driver -> provider chain had already worked end to
end on the first AP frame. The blocker was a **precondition object** the AP path inherited only by
accident. The fix (v7) posts the mode-1 VAP base (`g0 id1` key `0x0064` 48 B -> key `0x0065`
40 B) **always, once per module load, before any AP frame**, flag-guarded and fail-closed: if the
base cannot be posted, the sequence aborts before the wire. The exception boot cannot be repaired
host-side - a power-cycle and a fresh golden boot are mandatory (the 04:30 fire used one).

**Residual risk.** Whether the SSID worker or the AP-config worker faulted first is [U] - announce
consumption is at the SR/queue level, not proof the worker ran. The label bug is corrected in v7 by
a carrier-aware label callback so dmesg prints `ap_cfg` for the 184 B frame. The VAP-base posting
is once per module load, not once per `start_ap`; a second AP start without a reload would behave
as the old flow (untested, out of scope for the first rung).

---

### F13. The hostapd AP path is a capability ladder, not one op

**Symptom / context.** From 2026-11-26 20:30 to 2026-11-27 01:30, five hostapd fires failed with
almost the same two lines ("Could not configure driver mode" / "nl80211 driver initialization
failed") despite a clean iw-driven interface lifecycle. The isolation was manual: `iw dev wlan0
set type managed` / `set type __ap` both returned 0 with the driver's dmesg, but hostapd's run
produced no type-change message - so the failing step had to be inside hostapd's nl80211 setup,
before any driver op it expected.

**Work.** Each failure was decoded against the 6.18.60 cfg80211/nl80211 sources and fixed in the
ap-start provider (v2-v6), one gate per fire [V]/[V*]:

1. **ucode modules.** hostapd-mini is an OpenWrt wrapper: it loads `/usr/share/hostap/*.uc` from
   the wifi-scripts package. The bench image lacked them -> "Error loading ucode: Unable to resolve
   path for module 'common'". Bench workaround: push `common.uc`, `wdev.uc`, /usr/share/hostap/wifi-detect.uc;
   image fix: `CONFIG_PACKAGE_wifi-scripts=y` (the wifi-scripts package).
2. **NL80211_CMD_CONNECT advertisement.** cfg80211 advertises the CONNECT command only when
   `rdev->ops->connect` (or `.auth`) exists; hostapd's shared init gate refuses a driver with
   neither AUTHENTICATE nor CONNECT. A fail-closed `.connect` was added (advertisement only - the
   AP path never calls it).
3. **SET_INTERFACE semantics.** hostapd always issues `SET_INTERFACE(AP)` during init. The M1 op
   had two real bugs: it returned 0 without storing `wdev->iftype` (cfg80211_change_iface WARNs and
   later AP checks read a stale type), and it compared `params->use_4addr == -1` ("not specified")
   as a real request and refused. v3 stores the iftype and treats -1 as unspecified. For a VAP
   created as `type __ap`, the same-type call is a no-op that never reaches the op - the robust
   bench flow.
4. **`.mgmt_tx` gate.** hostapd subscribes to AP management frames with
   `NL80211_CMD_REGISTER_FRAME`; `nl80211_register_mgmt()` refuses every subscription when the
   driver has no `.mgmt_tx` ("not much point in registering if we can't reply"). v4 registers a
   fail-closed `.mgmt_tx`.
5. **`wiphy->mgmt_stypes` gate.** With `.mgmt_tx` present, the same path then calls
   `cfg80211_mlme_register_mgmt()`, which requires a non-NULL `wiphy->mgmt_stypes` and the
   per-iftype rx bit for every subscribed subtype. v5 advertises AP rx/tx = `0x3c15` (assoc/
   reassoc request, probe request, disassoc, auth, deauth, action) plus STA/monitor tables; dmesg
   confirms `mgmt_stypes advertised (AP rx=0x3c15 tx=0x3c15)`. Management TX stays fail-closed.
6. **`WIPHY_FLAG_REPORTS_OBSS` gate.** The 01:30 failure was "Failed to set beacon parameters" with
   no start_ap line. It was **not** SET_BEACON: the initial beacon is `NEW_BEACON`, and cfg80211's
   SET_BEACON path needs a non-zero AP beacon interval that only a successful start_ap sets. With
   userspace AP SME, hostapd first calls `nl80211_get_wiphy_data_ap()`, which registers
   `NL80211_CMD_REGISTER_BEACONS`; cfg80211 rejects that with -EOPNOTSUPP unless
   `WIPHY_FLAG_REPORTS_OBSS` is set, and hostapd returns -ENOBUFS **before building NEW_BEACON**.
   v6 sets the flag (registration bookkeeping only; the driver has no OBSS reporting).
7. **The `wiphy_new_nm` WARN.** The same fire showed a kernel WARNING at
   `net/wireless/core.c:532`; that line is `WARN_ON(ops->connect && !ops->disconnect)`. v6 pairs the
   v2 `.connect` gate with a fail-closed `.disconnect` stub.

**Interpretation.** hostapd's AP-ENABLED path is gated by **cfg80211 advertisements** - op presence,
`mgmt_stypes`, wiphy flags - not by `.start_ap` alone. Every gate fails with the same vague
hostapd line, so the provider grew dmesg instrumentation at SET_INTERFACE / start_ap / mgmt_tx /
module init. The final driver-side gate was F12 (the firmware VAP base); with it the same hostapd
flow that had failed five times printed `AP-ENABLED`.

**Residual risk.** The fixes are advertisements, not implementations: management TX/RX, OBSS
reporting, beacon control and WPA2 `add_key` remain M3/absent and stay fail-closed. `change_beacon`
is registered to accept SET_BEACON host-side and performs no firmware write yet. A real
AP-with-clients milestone needs the missing ops, not just the advertisements.

---

### F14. `AP-ENABLED` is not a link: the v4 milestone's honest boundary

**Symptom / context.** The 2026-11-27 04:30 fire is the project's headline milestone: hostapd
reported `wlan0: AP-ENABLED` with SSID `cudy-ap-test`, and the firmware consumed the four AP-start
messages in order with no exception. The risk is over-reading the word "AP".

**What is proven.** The full control-plane chain hostapd -> cfg80211 -> clean driver -> frame
provider -> HCC -> firmware real VAP object: (a) hostapd reached the driver's `.start_ap` and its
`-B` daemonization returned only after interface setup; (b) the four messages - vap_add 48 B,
vap_start 40 B, ap_ssid 64 B, ap_cfg 184 B - were posted and **consumed by the firmware in 0 polls
each**; (c) the read-only firmware readback is clean: golden page, state 1, mask 0, staging 0,
`ctx0+0x18C` changed (`0x70094810`), VAP pointer `ctx0+0x190` = `0x01010F18` nonzero; (d) hostapd
then reports `AP-ENABLED` and `iw dev wlan0 info` shows `type AP, ssid cudy-ap-test` [V*].

**What is not proven.** (a) **Beaconing**: the firmware-side beacon TX is not observable from the
same radio, and `AP-ENABLED` only means the start was accepted; no D2H beacon event with a known
meaning was recorded. The later persistence check strengthened the indirect evidence - the AP stayed
live for minutes and the firmware's D2H catalogue gained a new `g1 id0x23` 24 B family after the AP
start (absent at boot; AP/beacon-period candidate [U]) - but that is not an on-air proof.
(b) **Association**: no client exists; management TX/RX is fail-closed and `add_key` is M3, so
even an OPEN client has no implemented path. (c) **Data path**: the merged data-plane module's
first frame is POST-ONLY (section 6.12) - the MAC did not consume it in this record. (d) **Delivery
(added after the 06:30 flash)**: the ABI-correct image was flashed the same day and the AP came up
again from rootfs-native modules, so the "image/runbook only" caveat is closed - but the flash does
not add any over-the-air or association proof. (e) **Portability**: the AP runs on the PD4-patched
research firmware; the vendor
blob's mailbox dispatch cannot be programmed host-side, so this is a research artifact, not a
shippable stack [V*]/[I].

**Residual risk.** The natural next fires (beaconing observation with an external monitor, air-TX
with the radio started, client association) each need a new instrumentation class (external
observer or mgmt TX/RX). Until then the report must say "first live AP control plane", never "a
working AP"; the claim rules in TABLES T29 are the gate. **v5 note:** the five-message vendor order
(F17) programs the channel context first and is now the recommended AP-start path; the platform
still has no on-air proof without the external observer.

---

### F15. The RX path was never broken - the telemetry was reading the wrong block

**Symptom / context.** "RX captures 0" had been the report's standing negative since v2. The
inherited capture path polled CA 0x40040000 +0x4c/+0x50/+0x54 and saw zeros; the earlier decode of
the completion descriptor was also 4 bytes off.

**Work.** The rx-static lane followed the vendor machw object fill and every ring-pointer accessor
[V]. The device keeps its **live** host-MAC status and ring pointers in a *different* mapped block
than the config program: machw word 8 = **CA 0x40050000**, with +0x00 host-MAC interrupt status,
+0x08 norm free-ring consumer rptr, +0x0c small free-ring consumer rptr, +0x10 completion-ring
producer wptr. The block the 14-write program touches (CA 0x40040000) carries the config words
(ring addresses/sizes, host-owned wptr/rptr, W1C status +0x44, mask +0x48 = 0xfffffcc1); its
+0x4c/+0x50/+0x54 are **never read by the vendor** and the stock live dump keeps them 0 while the
live block reads 0x20 - the MAC had already prefetched 32 free buffers. The completion entry is
the **devVA of buffer+4**, and the **36-byte device header** is at buffer+4: payload length at
header+0x1a (buffer+0x1e; gate len+0x28 <= 0x640), next sub-MSDU at header+0x20, MPDU at
buffer+0x28. There is **no extra RX enable bit or H2D RX message** - the 12 vendor free/completion
ring writes are the whole device enable; the missing half was always the **consumer** (rptr
advance/refill), implemented behind the fail-closed `rx_enable=1` + `dp_rx_arm`/`dp_rx_drain` path
[V].

**Bench (2026-11-28 21:00).** Fired on a fresh golden boot + AP-ENABLED: `DP rx_arm: live bank
CA 0x40050000 status=0x00000182 rx_pending=0 mask=0xfffffcc1 ... norm_rptr=0x0020
small_rptr=0x0020 cmp_wptr=0x0000 ... config: norm_wptr=0x8000 small_wptr=0x8000
cmp_rptr=0x0000` - **the exact vendor stock mirror**; the free-ring consumer movement proves the
MAC consumed published free entries, i.e. the MAC RX consumer is running [V*]. `dp_rx_drain` moved
no pointer because there were no completions yet: no on-air traffic exists in the bench setup.

**Interpretation.** The inherited "no RX" conclusion is invalidated: the device path is armed and
its consumer is proven; the missing ingredient is *traffic*. This unblocks the WPA2 4-way PTK, the
AP-side assoc/auth RX and air-TX completion evidence once an external client or a host-generated
frame reaches the MAC.

**Residual risk.** The sub-MSDU chain accounting (header+0x20) is [U] and the patch refuses chains
(`HIL_RX_ECHAIN`); netdev/MSDU delivery is not implemented (capture-only); which frames the
firmware forwards to the host rings (mgmt vs data) is [U].

---

### F16. WPA2-AP: the GTK installs at AP start; the PTK is RX-gated

**Symptom / context.** v4 left "WPA2 key install" open (M3), assuming it needed association or RX.
The wpa-static lane showed hostapd's AP path installs the **group key** immediately after
NEW_BEACON, before any client exists [V].

**Work.** The vendor's cfg80211 key ops each post **one g0 id1 config event** (no host MAC-register
sequence) [V]: `add_key` = cmd **0x0078** (body **0x44**: key_index, pairwise, MAC[6], key[32],
seq[16], key_len@0x38, seq_len@0x3c, cipher@0x40), `set_default_key` = **0x0079** (0x28; index,
unicast, multicast class), `del_key` = **0x007A** (8 B). Firmware table-A rows dispatch them to
workers **CA 0x0A5825 / 0x0A5C9D / 0x0A4271**; the worker classifies the cipher by the suite's low
byte (data 0x0714 / mgmt 0x3840 / WEP 0x0022) and stores the key in the per-user key cache. The
2.4.15 and 2.5.24 host modules are instruction-identical at +0x208 for these ops. The patch adds
`.add_key`/`.del_key`/`.set_default_key` (cfg80211 requires all three together or it WARNs) with
constructors, provider caps and a `wpa_keys=0` fail-closed default; key material is never logged
[V].

**Bench (2026-11-28 23:00).** WPA2 hostapd AP, `wpa_keys=1`: `AP-ENABLED`, then
`frame post key_add: g0 id1 total 96 B ... consumed after 0 polls` (idx=1, pairwise=0, cipher
0xfac04 CCMP, key_len=16) and `frame post key_default: g0 id1 total 68 B ... consumed after 0
polls` (idx=1, uni=0, multi=1). Page golden, mask 0, staging 0, no exception [V*]. The group key is
installed and selected as the default multicast key - **the predicted M3 unblock is hardware-proven**.

**Interpretation.** WPA2-AP is now a *security-setup* milestone, not a blocked feature: a WPA2 AP
starts and installs its GTK. The 4-way handshake (pairwise PTK) needs a client's EAPOL frames, so
it remains RX/association-gated; the AP-side auth/assoc exchange still needs host management TX
(`.mgmt_tx` is fail-closed) and MAC RX.

**Residual risk.** Only the OPEN/WPA2-PSK GTK path is exercised; `set_default_mgmt_key` is
unregistered (vendor builder leaves a cipher byte the firmware mgmt path wants), `get_key` reply
layout is [U], and no client/PTK test has been run.

---

### F17. The vendor AP start is five messages, channel first - and the firmware beacons autonomously

**Symptom / context.** v4's AP start was four messages with the mode-1 VAP base posted before the
AP config and **no channel config**. The AP reached `AP-ENABLED` but no on-air beacon was ever
observed. The beacon-static lane asked what the host might be missing [V].

**Work.** Reversing both vendor host modules gives a strict **five-message order** [V]:
**channel config WAL cmd 0x0124 FIRST** (4-byte body {band, mac_vap+2, channel, 0} -> module ctx
+0x10..+0x13 via worker CA 0x841b8) -> SSID **0x0005** -> auth **0x006D** (WAL envelope only; the
g0 carrier has no static key) -> AP config **0x0127** -> **VAP start 0x0065 AFTER the AP config**
(worker CA 0x46394). The host never supplies a beacon template: the firmware image carries the SMAC
TBTT ISRs and beacon suspend/resume handlers, and the beacon frame is assembled at runtime from the
VAP context; there is no bcn-ring/beacon-table message in the AP start path [V]. The clean-room gap
was therefore exactly the two missing/ordered steps, not a missing beacon message.

**Bench (2026-11-28 22:00).** The first fire of the lane's patch hit a false-negative guard:
`module_ctx+0x190` still read zero 100 ms after a fresh VAP base post, while a *successful* AP
start also reads zero there - the field is asynchronous and not a VAP-validity indicator. After the
guard fix (log-only for a fresh post, fail-closed only for a stale pre-existing latch), the re-fire
executed the vendor order on a fresh golden boot: `plan (5 steps): vap_add ap_chan ap_ssid ap_cfg
ap_vap_start` -> `ap_chan posted (32 B, band=0 channel=6) - the firmware module channel context is
programmed BEFORE the AP config` -> `ap_ssid` -> `ap_cfg` -> `ap_vap_start posted after ap_cfg` ->
hostapd `AP-ENABLED`, ssid `cudy-ap-test`, type AP; page golden, mask 0, staging 0, no exception
[V*].

**Interpretation.** The AP-start model is corrected: five messages, channel first, VAP start last.
The program now starts the AP with a configured module channel and a VAP context that already holds
the AP parameters - the static candidate for the missing on-air beacons is removed from the host
side. Beacon generation itself is **firmware-owned and autonomous**; proving it on air still needs
an external observer, and a usable AP still needs the AP-side management TX + RX path (F15/F16).

**Residual risk.** On-air beacons are not proven; the channel readback is log-only (garbage value
observed, non-gating) and the live-bank window is a candidate better source [I]; the module-global
VAP-base latch and the absent `stop_ap` still constrain in-boot restarts (C55); a single AP start
per fresh boot remains the safe bench rule.

---

### F18. The full 13-channel scan is structurally unreachable on this firmware

**Symptom / context.** v4's scan work got real results but ended `finish-abort[host-timeout]` after
4/13 or 7/13 channels. The scan-tuning lane's H1 band-config experiment reached 7-8 channels; the
static wave was asked whether programming the per-slot context array (8 slots) could reach 13.

**Work.** The scan-full lane decoded the g0 id10 handler completely [V]. The message is the vendor
host's relay of received **ADDBA response / DELBA** management frames (`hmac_mgmt_rx_addba_rsp` /
`hmac_mgmt_rx_delba`), not a channel plan: payload[0]=3, payload[1]=1|2, object via 0xD6A1E, slot
0..7, per-slot contexts at `obj+0xEC+slot*0x20` (8 x 0x20 B). The **scan engine**
(`[[module+0x4B8]+0x1A00]`) reads only the context at 0xEC = **slot 0** (+0xFC start, +0xFD step,
+0xFE end/count, +0x103 mode, +0x104/0x105 result counters, +0x106 stride default 6), and **no
subtype-1 payload field writes +0xFE** - only the subtype-2 clear path writes it (to 0). More slot
configs therefore cannot widen one request. The lane also corrected the R4 decode: the dispatch
row is CA **0x10CD5C** (table at 0x10CCFC; 0x10CD08 is id 1), the vendor wire shape is **44 B**
(32 B payload, not R4's 36 B), the subtype-2 flag is payload+0x14 (not +0x08), and the H1 effect
is **pacing** (payload+0x10 presets the result counter +0x104 and +0x12 sets mode), not a channel
count [V].

**Bench record.** The H1 pacing config does re-arm the scan to 7 channels (8 with a larger counter
preset, results=0) and mode=1 gives a dense/passive 4-channel/14-BSS yield; page golden and mask 0
on every rung [V*]. No combination reaches 13; the honest ceiling is **6 channels/request without
H1, 7-8 with H1** (off by default). Batching 6+6+1 across requests is the shipped fallback.

**Interpretation.** This closes the scan line as an evidence-backed negative result: no further
fires should chase 13/13 via g0 id10 slots. The remaining single-request 13-channel path would
need a different host-visible firmware interface (e.g. the sibling g0 id11 handler CA 0x5E2F4's
channel bindings) or accept batching; both are documented, neither is claimed.

**Residual risk.** The 146-object pool identity (BA session context vs other) is [I]; g0 id11's
channel binding values are [U]; the H1 message stays opt-in (`scan_band_cfg=0` default).

---

### F19. STA / 5 GHz / HT-VHT: the model is decoded; capability is not a working client

**Symptom / context.** v4's driver had `-EOPNOTSUPP` connect stubs, a one-wiphy-per-endpoint model
and only HT20. The static wave was asked to map STA mode, the 5 GHz endpoint and HT/VHT.

**Work.** The connect path is a **WAL cfg-event**, not a direct HCC message [V]: the same group-2 /
WAL-id-1 envelope the bench-proven AP-start frames use, with **opcode 0x0123** and a fully mapped
**108-byte body** (ssid_len/mfp, IE/SSID/BSSID/key host-buffer addresses, privacy, auth_type,
key_len/key_idx, ie_len); disconnect is **opcode 0x00d0** with a 16-byte body {bssid, reason,
[15]=1} - the inherited allowlist's "KICK_STA" row is really cfg80211 disconnect. The vendor is a
**connect-SME** driver: `.auth`/`.assoc` are NULL and the **firmware** authenticates/associates;
completion is a D2H management event our RX path does not decode, so the patch posts the join but
never fakes `cfg80211_connect_done`. 5 GHz is **one wiphy per band**, not per PCI endpoint: the
vendor loops over the board band list and creates one wiphy per band on chip 0 (two RF chips behind
one host MAC), which produces the same two-wiphy observable as our endpoint model. Vendor band
tables: 2.4 GHz **14** channels, 5 GHz **29** channels (incl. ch144 and the 4.9 GHz 184-196 block);
HT cap **0x0062** (20/40 + SGI20/40), VHT on both bands, 2 SS MCS0-9 (**0xfffa**), VHT cap
**0x00400000** (2.4 GHz) / **0x00400064** (5 GHz: HTC_VHT + 160 MHz + SGI80/160, matching the live
ch44 @160 MHz readout) [V].

**Interpretation.** The static model is complete enough for a fail-closed implementation
(`sta_join` 0/1/2, `ht_mode` 0/1/2, `vht_cap`, `band_vendor`; WAL allowlist +{0x0123,108}), but
advertising HT40/VHT is a capability only: actual width use needs the firmware-side channel-width
configuration, and a working STA needs the D2H management completion + RX/EAPOL + WPA keys. The
patch is host-built/compile-verified and **has not been fired as a standalone connect rung** at the report cut-off, although it is included in the final all-patch module (6.18) with `sta_join=0` (join unexercised).

**Residual risk.** The four host-buffer addresses in the connect body have an [U] firmware
dereference contract (the first post is the experiment); the 5 GHz firmware channel-id table is
[U]; the legacy g0 id0xF join reference fields are pre-staged in firmware memory and not
expressible host-side; EAPOL depends on the RX path.

---

### F20. The final all-patch module: one clean-room module, every feature validated in one boot

**Symptom / context.** By 2026-11-28 the static-RE wave had produced four feature patches (rx,
wpa, beacon order, sta) on top of the tuning chain, but each had been fired as a **separate**
module. The remaining integration question was whether the features are additive - one module, one
boot, with the AP, the keys, the data plane and the RX path all live together - or whether they
conflict.

**Work.** The stability lane built the **final all-patch module** by re-staging the full chain and
applying the patches in order (base -> image-polish -> dp-tuning -> scan R4 -> stability ->
rx-static -> beacon-static + guard fix -> wpa-static -> sta-static), documenting six additive
conflict resolutions and shipping a reproducible build script that refuses drift [V]. The result
is **hi5622_wifi.ko stripped 308,080 B sha256 `5b587949...`, 47 module parameters, 0
compiler/modpost warnings, cfg80211 contract 10** (opensource `008aca5`; the lane's
FINAL-INTEGRATION.md + build evidence are the record).

**Fired (2026-11-29 01:00).** One fresh PD4 golden boot with the full parameter set
(`allow_frames=32`, `h2d_send_budget=10`, `vap_start_first/mode=1`, `hcc_trace=0`,
`stop_ap_mode=0`, `rx_enable=1`, `beacon_static=1`, `beacon_vap_start=1`, `wpa_keys=1`, `sta_join=0`,
`ht_mode=0`, `vht_cap=0`, `band_vendor=0`, `scan_band_cfg=0`):

1. **AP, vendor five-message order** - `plan (5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start`;
   `ap_chan` (32 B, band=0 channel=6) programmed the module channel before the AP config;
   `ap_vap_start` posted after `ap_cfg`; **hostapd AP-ENABLED (rc=0) with the WPA2 config**,
   ssid `cudy-ap-test`, type AP.
2. **WPA2-AP GTK** - `key_add` (g0 cmd 0x0078, 96 B, idx=1, pairwise=0, cipher 0xfac04,
   key_len=16) + `set_default_key` (0x0079, 68 B, idx=1, multi=1), both consumed in 0 polls by
   firmware workers CA 0x0A5825/0x0A5C9D.
3. **TX COMPLETED** - `dp_tx_complete_poll verdict=COMPLETED` with the TXQ publish enabled
   (table `{wptr=1, rptr=1}`).
4. **RX arm PASSED** - live bank CA 0x40050000, status 0x00000182, mask 0xfffffcc1, norm/small
   rptr 0x0020 (exact vendor mirror); free-rptr movement proves the MAC consumed published free
   entries.
5. **Post pack** - page `0x86807FEF` golden, state 1, mask 0, staging 0, no exceptions.

**Interpretation.** The clean-room stack is feature-complete at the control/device-plane level in
a single module: the vendor AP sequence, WPA2-AP group-key security, the completing TX ring, the
proven RX consumer, the scan path, the STA/5GHz/HT parameters, the stability guards and the polish
defaults coexist without conflict. This is the project's culminating validation.

**Residual risk / remaining gaps.** The air-side proof is still external (beacon/association/
4-way PTK need a client or an observer); RX has no captured frame yet (no traffic); the STA join
was not exercised (`sta_join=0`); the 5 GHz band config was not fired; and the final module still
has to be baked into the self-contained image. None of these changes the feature-complete status
at the device level.

---

## 5. Protocol reference (summary)

The full data tables are in [TABLES.md](TABLES.md); this section is the narrative summary needed
to read the operational loop in section 6.

### 5.1 The frame

Every HCC message is a 12-byte header + payload. Total length is `payload + 12` at `+0x04`;
the dispatch id is a u16 at `+0x06`; the transport tag `0x5A5A` is at `+0x0A`; the group is the
low nibble of byte 0. Header byte 1 splits into an alloc nibble (vendor 1) and a resource/core
nibble; `hcc_msg_tx` copies the high nibble to byte 9 [V]. A frame is valid iff the tag is
`0x5A5A`, the total equals the ring-node length, and the dst core matches [V].

### 5.2 Groups and ids

| group | direction | use |
| ---: | --- | --- |
| 0 | host -> device | config/control events; the H2D receive table has 41 slots (40 + early 0x2F) |
| 1 | device -> host | firmware responses/acks (observed: id3 36 B, id0x12 40 B) |
| 2 | host -> device | WAL/scan/MLME class commands; 19 registered H2D slots |
| 3 | device -> host | hdpp/hmac data-path and event frames (e.g. id0x12 temp-state, 16 B) |
| 4 | device -> host | chip boot/heartbeat: id0 OAM log, id1 plat ready, id2 DEVICE READY, id3 exception info, id4 heartbeat |

The two messages of the operational loop are both group 0 id 1 config events distinguished by
their key: **key `0x0064` = context-create**, **key `0x00D8` = TXQ config sync**. Keys live at
payload+8 (`u16`), length at payload+0xA, data at payload+0xC [V].

### 5.3 The H2D mailbox

To send, the host commits a node in an SR ring, then announces mailbox **id 3**: it ORs
`1 << 3` into `out[0]` (CA `0x40039010`) and rings the doorbell (`out[2]`, CA
`0x400392D4`, bit 0). It then polls `out[0]` until the firmware clears the bit. Mailbox id 1 is
a payload-free liveness/PM wake; id 5 is a firmware `BUG()` trap and must never be posted; id 6 is
the firmware's own reply primitive; ids 0,2,4,7,8,9 are unregistered and dropped. The host must
**never write CA `0x400392F0`** - that is the firmware's own dispatch ack pulse [V].

The device's H2D dispatcher is entered through the image vector handler on event 0x4C, which is
why F3's patch is needed on our boot; on a vendor boot the vendor host's service loop keeps the
event path alive.

### 5.4 The D2H service

Vendor order: see `out[1]` bitmap -> write 1 to `out[3]` ack (CA `0x40101438`) -> read/drain
-> write 1 to `out[4]` re-arm (CA `0x40101414`) -> dispatch lowest-id-first. Handler table ids
{1..7}; ids 0/8/9/10 dropped; id > 10 stops the word. On our stack the service is polled (no host
MSI/INTx demux on 6.18 yet) and the direct-read path (F4) supplies the frames.

### 5.5 The ETE rings

SR0-SR2 carry host->device nodes; DR0-DR3 carry device->host nodes. A node is 8 bytes:
`word0` = device-side address of the host-DRAM buffer, `word1 = (len << 16) | 0x6D2B`.
Indices are packed: 10-bit counter plus a `0x400` wrap-phase bit; depth 32 (`0x1F`). The host
must arm the rings and the D2H service **before** release so the ready handshake is not lost [V]/[V*].

### 5.6 The boot registers, in one place

| object | address | host action |
| --- | --- | --- |
| release | device CA `0x40000108` | write `0x00005A5A` once, after upload + seeding |
| boot gate | device CA `0x4000010C` | **do not write during boot**; later, evidence-driven `0xCECE` only |
| handshake page | CPU `0x1BF000` | read-only golden gate (`+8 == 2`) |
| exception mask | CPU `0x40008` | read-only safety gate (must stay 0) |
| mailbox | CAs `0x40039010/14`, `0x400392D4`, `0x40101438/14/34`, never `0x400392F0` | see 5.3/5.4 |

### 5.7 The Wi-Fi frame plane in one paragraph

Wi-Fi frames do **not** travel in HCC messages. The HCC/ETE plane carries control and event
messages only; the frame plane is the MAC host-interface ring block at device CA `0x40040000`
(band 0) / `0x40060000` (band 1), slot 5. The ready handshake's `g4 id2` payload is the
MSDU-info ring pointer table (ACP `0x02060850`) and the vendor's next frame after ready is the
TXQ-table address sync (`g0 id1`, key `0x0320`). TX posts 8-byte MSDU-info entries into per-TID
rings; RX free-buffer and completion rings deposit 802.11 MPDUs behind a 0x28-byte device
descriptor. Section 6.4 and [TABLES.md](TABLES.md) T24 give the full geometry; the direct-read
constraint of section 5.4 applies to ETE messages only [V]/[I].

---

## 6. The operational duplex loop and the interface it enabled

### 6.1 The message-level loop (2026-11-25 05:10)

This is the session shape that closed the Wi-Fi-side RE goal. It runs
on the PD4-patched firmware image and the clean-room driver (`hi5622_wifi.ko`, 400,732 B, sha256
`774c4f1f...`), with `h2d_send_budget=2` [V*].

```
S0  fresh boot of the instrument image (single clean endpoint reset pulse)
S1  insmod; prepare six iATU viewports (read back 6/6)
S2  upload 928,920 B firmware; verify target window (diffs = 0)
S3  pre-seed platform block (24 words) + wifi-custom block (618 words); read back
S4  hcc_arm (ETE rings, IRQ mask RMW); hcc_loop started; release 0x5A5A ONLY
S5  golden gate: page 0x86807FEF/7/2/0xABCD, state 1, mask 0  (boot_latched = 1)
S6  ready handshake decoded by the direct reader: g4 id1 then g4 id2 (payload 0x01060850)
S7  PACK BEFORE (read-only): ctx190 = 0, ctx0 = 0, table entry = 0x0011C458, mask = 0
S8  ONE h2d_ctx_create type 1  (g0 id1, key 0x64)
      -> consumed in 0 polls; ctx+0x190 = 0x01011448, ctx[0] = 4, ctx+0x434 = 0x0100478C
      -> mask = 0, staging = 0
S9  ONE h2d_txq_sync72  (g0 id1, cmd 0xD8)
      -> consumed in 0 polls; out0 = 0, out2 = 0, out5 = 0
      -> mask = 0, staging = 0 (NO exception frame)
      -> h2d_status ok=2 timeout=0 refused=0; phase ACTIVE
S10 read the direct D2H catalogue:
      NEW g1 id3  (36 B)  and  g1 id0x12 (40 B)   <- the firmware answered both messages
S11 verify boot remains golden: page/state unchanged; g+0xB4 = g+0xB8 = 0
```

The two response frames are given byte-exactly in [TABLES.md](TABLES.md) T13 (F5/F6). They have
well-formed HCC headers (tag `0x5A5A`, group 1, lengths matching the node lengths) - the firmware
processed our messages and produced protocol replies.

The state machine that encodes this is the committed `hcc_core` (528-check selftest):
`IDLE -> BOOT -> WAIT_PLAT_READY (<= 2.0 s) -> WAIT_DEVICE_READY (<= 20 s) ->
READY_TXQ_SYNC -> ACTIVE`, with the latched boot gate, the vendor-order D2H service, H2D
coalescing, the evidence-only CECE policy, heartbeat and PM as separate, testable steps [V].

### 6.2 Calibration ladder C1: the firmware answers the first calibration message

The vendor boot sequence after the TXQ config sync is the factory calibration: six `alg set_*`
config events, HCC **group 0 / id 2, key `0x0101`**, each carrying a fixed 270-byte alg_cfg blob
so the frame total is **298 B** and the SR node declares `0x012A6D2B`. The blob layout is
`u16 cfg_id | u8 type(SET=0) | u8 pkt_type | u32 dev_ret | u8 need_w4 | u8 resv | value[260]`;
the value area starts at blob+0x0A and the request's header field B selects the module/vap object
(default 4, the object our type-1 context-create made). The six vendor commands and cfg ids are:

| order | command | cfg id | value bytes |
| ---: | --- | ---: | ---: |
| 1 | set_2g_power_param | 3502 | 104 |
| 2 | set_2g_low_power_param | 3503 | 104 |
| 3 | set_5g_power_param | 3504 | 70 |
| 4 | set_5g_low_power_param | 3505 | 70 |
| 5 | set_xo_ppm_cali_param | 3506 | 4 (u32) |
| 6 | set_rssi_param | 3507 | 40 |

All six are SET/device-processed and expect exactly one answer: an HCC **g1 id3** config-sync
frame with `syn_id 0x0101`, `body_len 270`, the request cfg id echoed and `dev_ret = 0`; no host
reply is sent. g1 id3 is a generic config-sync channel: a backend must dispatch it by `syn_id`
(`0x0101` alg, `0x0102` cali, `0x07xx` per-VAP capability sync), never by frame size [V].

**Bench (ladder C1, 2026-11-25 12:30).** On a fresh PD4 golden boot with the calibration-token
driver (407,996 B, sha256 `2d45e545...`, 301/301 + 251/251 selftests, `h2d_send_budget=3`):
context-create type 1 and TXQ sync consumed in 0 polls, then ONE `set_rssi_param` (3507,
zero-value structural frame) committed as a 298 B frame in a 298 B node and consumed in 0 polls;
the device ring catalogue then showed a **second g1 id3 with `last_total = 0x012A` (298 B)** -
the calibration answer mirroring our request length. Boot page `0x86807FEF/7/2/0xABCD`, state 1,
mask 0 stayed intact; `h2d_status` ok=3 timeout=0; no exception. The answer body was recycled
before the read-only capture, so body semantics (cfg id echo, dev_ret) are expected-by-construction
rather than observed; a future fire can capture it with an immediate post-fire read [V*].

**C2 (2026-11-25 16:30; automated 23:40).** The full vendor-order six-message boot (cfg ids
3502-3507, `h2d_send_budget=8` = create + TXQ + six calibrations, the driver maximum) ran on a
fresh PD4 golden boot. Every write was consumed in 0 polls (`h2d_status frames_sent=8 txq_sent=1
ctx_sent=1 cfg_cal_sent=6 ok=8 timeout=0 refused=0`, all announces done); `rd_catalog g1 id3`
showed **count=7, last_total=0x012A (298 B), last_len=298** - one unsolicited frame plus a 298 B
answer for each of the six requests; mask 0, staging 0, page/state golden, `safety writes=67
bad_writes=0`, heartbeats continuing. **The vendor calibration boot sequence now runs end-to-end on
our clean-room host** [V*]. The automated phase runner later reproduced it as phase C2 with
**54/54 checks in 7.02 s** (2026-11-25 23:40, section 6.8). The answer bodies remain
count+length evidence (the buffers are recycled before a read-only capture). Values policy is
unchanged: only zero-value structural frames are described here; real per-unit calibration stays
local (section 11).

### 6.3 The first real Wi-Fi interface on our own image (2026-11-25 15:00)

This is the milestone that turns the protocol into a user-visible Wi-Fi interface.

**Image build.** The target kernel config gained `CONFIG_CFG80211=m` (plus the resolved
WLAN/RFKILL/signed-regdb symbols). The board image built clean: `cfg80211.ko` 2,328,548 B
sha256 `949393f9...`; sysupgrade container 6,267,199 B sha256 `22b72852...`; the kernel slice
extracted from it is 3,372,638 B sha256 `13e81214...` with full-slot hash `1794a531ed19...`.
The clean driver was rebuilt against that tree (505,076 B sha256 `d2b3cf79...`,
`depends=cfg80211`, 0 warnings) [V].

**Flash pipeline (bench-only, attended).** The box was running our GP1-line image on pair B and
stock on pair A. The new kernel slice was fetched by the stock pair, written to **kernelb** with
`mtd erase/write` and verified with the full-slot hash, the pristine stock files were restored,
the handover used the documented route (`bootflag=b` + `boot_reg=0x21`), and the box rebooted.
Stock pair A kernel+rootfs remained untouched (the write went through stock's own mtd), so the
physical recovery path stayed intact; the recovery door was armed during the flash. Exact
artifacts, hashes and steps: table T21 and `wifi-upstream-target/CFG80211-BENCH-IMAGE.md` [V*].

**Bench result.** The new kernel came up on our image (`uname -r` 6.18.60, rootfsb, selector raw
`0x11`) with the GP1 in-probe endpoint pulse visible in dmesg. `insmod cfg80211.ko` then
`insmod hi5622_wifi.ko` both returned 0; the driver allocated one wiphy per PCI endpoint (2.4 GHz
13 channels / 5 GHz 19 LU-plan channels) with registration **gated on the ready handshake**. After
the standard boot sequence (prepare/upload/verify/seed/arm/loop/release) the golden page and ready
pair were reproduced, dmesg printed `cfg80211: ready handshake decoded ... scheduling wiphy
registration` then `cfg80211: wiphy registered after the ready handshake`, and
`/sys/class/ieee80211/phy0` appeared. The stock `iw` binary (121,952 B, pushed to /tmp with
libnl-tiny) enumerates `Wiphy phy0`; `iw phy phy0 interface add wlan0 type managed` created a
real netdev (ifindex 7, type managed, locally-administered documentation address) and
`iw dev wlan0 del` removed it. **M0 (wiphy+netdev) and M1 (VAP lifecycle) are
hardware-validated on our own image with the vendor firmware running golden** [V*].

**Honest limits.** At the v2 cut-off this was the management interface only: no firmware VAP
object, `-EOPNOTSUPP` scan/station stubs (M2), every M3+ op NULL, no TX/RX data path. Later
sessions closed part of that gap (sections 6.7-6.8): real `g0 id7/8` frames create the
mailbox-layer VAP, the 520 B `g0 id0xC` scan request is posted and consumed, and the MAC data
plane is programmed host-side with its TXQ address sync accepted - but the per-radio VAP **start**
object, scan results, association and any TX/RX frame are still open. The cfg80211 kernel config is
bench-only and is not part of the luofu kernel patch series (section 11). The wiphy band mapping
(domain 0 -> 2.4 GHz) is an inference with a `band_force` override; the bench saw both bands
listed on the two endpoints, not a per-band link test (C26).

### 6.4 The data-plane architecture (mapped, host-implemented, first frame ring-published)

The frame plane is fully mapped statically from the vendor host driver and the firmware and has now
been added to the clean driver by the datapath-bringup lane (`dp_probe`/`dp_setup`/`dp_rings`/
`dp_txq_sync`); its setup + TXQ-table address sync passed the automated E1 phase on hardware. v4
adds the merged data-plane provider: the host published its **first frame to the MAC TX MSDU ring**
on 2026-11-27 02:30 (POST-ONLY - the MAC did not consume it in that record; section 6.12, table
T31). **No frame has been completed or received by our host** (details in `wifi-datapath/REPORT.md`,
`wifi-datapath-bringup/REPORT.md` and `wifi-dp-air/`, tables T24/T26/T31).

**Two planes.** HCC/ETE = control/event messages (working). MAC slot-5 host-interface block =
Wi-Fi frames (host setup + TXQ address sync validated by E1; no frame has moved yet). The blocks are
disjoint: `plat.ko` writes only ETE CAs, `wifi.ko`
only MAC CAs. The direct-read workaround is therefore a message-plane workaround; MAC DMA deposits
are expected to work independently once the block is programmed [V]/[I].

**Seeding.** The ready handshake's `g4 id2` payload `0x01060850` is the device-RAM address of the
MSDU-info ring pointer table; the host maps it as ACP `0x02060850` and writes that to MAC slot-5
`+0x10` for both bands. The vendor's next frame is the TXQ-table address sync: `g0 id1`, key
`0x0320`, 4-byte device VA of a 0x2524-byte host-DRAM TXQ table [V].

**TX.** One non-cached ring per (user,TID); each 8-byte entry is
`{u32 buf_devva; u32 ctl}` with `ctl[11:0]` length, `[14:12]` TX path class, `[15]` header flag,
`[19:16]` tid and `[21:20]` vap type (vendor 1/2/3). The host builds a 20-byte DMA descriptor at
`skb->data-0x14`, maps the frame buffer, posts the entry, and publishes the packed producer index
(15-bit index + phase bit 15) with device-visible barriers. Completions arrive through the
256-entry `tx_ba_info` buffer and/or core-g0 TX events; the 16-byte completion record is the
natural first parse target [V].

**RX.** Host pre-fills `rx_norm_data_free_ring` (256 x 0x63c B) and `rx_small_data_free_ring`
(512 x 0xb0 B), advances the packed +0x38/+0x3c write pointers, drains
`rx_data_cmp_ring` (4095 entries) via +0x40/+0x4c, parses the 0x28-byte device descriptor at the
start of each buffer, strips it, handles A-MSDU chains, resolves VAP/user/tid, converts 802.11 to
Ethernet and reposts the buffer. The MAC interrupt status/mask (+0x44/+0x48, mask value
`0xfffffcc1`) is the polling alternative to ETE completion [V].

**Control messages around the data path.** `g0 id7/8` user-add (0xA8 each), `g0 id9` del-user
(0x0C), `g0 id0xC` scan request (0x1FC), `g0 id0xF/0x10/0x11` STA join family (0x40/0x10),
`g0 id0x12` EDCA (0x74), `g2` WAL config events (AP start etc., ids [U]); D2H results are
`g1 id15/16/17`, `g2 id2` and `g2 ids 3-6`. Only the config-event shape is byte-exact today; the
open payload structs are the wall for VAP/scan/AP. The 217/88-entry key tables A/B decode this
namespace; every vendor host key from the shipped modules resolves in them [V]/[U].

**Bring-up order proposed by the lane.** D0 MAC register model -> D1 RX rings + NAPI skeleton ->
D2 TX ring + TXQ address sync -> D3 user/VAP messages -> D4 scan -> D5 STA join, with a
**read-only** first check (ladder B2: single read of CA 0x40040000 and of the ready-seeded +0x10)
before any host write, because an earlier attempt to add an extra iATU viewport rebooted the box
[I]/[V*].

**Bench (2026-11-26 03:00, automated phase E1).** The extension module (462,952 B sha256
`8fc693ad...`, 0 warnings, host 69/69, static decode 34/34, inherited H2D 308/308) was deployed by
the phase runner and the E1 ladder ran **PASS, 37/37 checks, 0 failed**: `dp_probe` (one
read-only word of MAC band-0 `0x40040000`) read clean; `dp_setup` (write 1) allocated the rings and
the TXQ table, wrote the 8 MSDU-info table entries through the ACP alias with read-back and
executed the 14-register MAC program in vendor order with per-write read-back (`dp_status`/
`dp_rings` p0-p3 gates PASS); then `dp_txq_sync` sent **one g0 id1 key-`0x0320` frame** carrying the
4-byte TXQ-table device VA, and the firmware accepted it with `txq_addr_sent=1 ok=2 timeout=0`
(no retry); the post pack showed mask 0, staging 0 and the golden page. **The MAC data-plane host
setup and the vendor's TXQ-table address sync now run end-to-end on hardware in the automated
runner** [V*]. What it does *not* show: no TX ring entry or RX buffer has moved a frame, the RX
completion ring and the descriptor format are unconsumed [U], the rings assume user index 0 (a
user/VAP that queues frames is needed first), and band 1 is untouched (T26).

### 6.5 The ETE engine: the host lever is closed, direct read is the supported RX path

**Trigger chain (fully reversed).** The firmware reads the ETE status word `CA 0x4003A874` only
in the event-0x2E/0x2D dispatchers (CPU `0x4624C`/`0x462F8`), which are reachable only through a
delivered device hardware event. Status bits **24-28** call the sending trigger CPU `0xC625A`,
which reads the hardware-authored send word `CA 0x4003AC00+4*ch` (target = high half), handshakes
the target index into the channel's PKTRAM control word, releases tagged nodes and wakes the
message object. Whole-image and vendor-host scans find **no** software writer of the status word,
the send words or the engine doors/global: the start is device hardware/ROM state [V].

**The one host-owned delta and its test.** The DR group-B destination rings have exactly one
host-owned field: the published index `+0x38`. Our clean driver arms it to `0x400` (32 pre-posted
buffers) while the vendor init writes 0 and a working vendor boot shows `published == fill`. Ladder
E1a wrote `+0x38 = fill` on all four channels (read-back OK) and E1b wrote 0 on a fresh boot; on
both, the engine status bits stayed 0, the trigger counters and send words stayed 0 and the fills
stayed `0x10` - the SR/mailbox notification path kept running, the DR copy never started. The engine door
`CA 0x4003A7C0` differed from the vendor value (`0x844C0000` vs vendor `0x83FAC000`) and has no
host writer [V*].

**Verdict.** The DR (D2H) A->B copy is firmware/ROM-internal; no host register work is justified.
The direct device group-A read is the supported RX path (and the only one on this firmware), with
the limits stated in F4: non-vendor ABI layout constants, polled (no DMA to host DRAM), no engine
flow control (bounded batches, overrun reporting), read-only on device state. The SR (H2D) A->B
copy, by contrast, runs normally - that asymmetry is what keeps the H2D send path working [V*]/[I].

### 6.6 The PD4 research patch: audit and boundary

The PD4 patch is the reason the H2D mailbox works on an IRQ-dead boot (sections 3.8, F3, F10).
The audit has now been delivered by the fw-audit lane (shared task `task-115`) as
`wifi-fw-audit/REPORT.md` plus a reproducible harness (`verify_pd4.py` 31 checks + 14/14
selftest; `pd4_variants.py` 14/14). Its verdicts:

* **Identity.** The published image (`cfe24bfb...`) rebuilds byte-identically from the pinned
  vendor blob with exactly two segments: the 4-byte entry branch at file `0x0916A` and the 64-byte
  stub at file `0x0C8240` - 68 patched bytes, 59 differing byte positions, length unchanged,
  nothing else differs [V];
* **Cave safety (independently re-run).** The stub lives in the 637-byte all-zero static-data gap
  CPU `0x108197..0x108413`, outside both boot memsets and the exception-staging frame
  `0x4000C..0x4004B`, between two referenced read-only tables; the exhaustive Thumb census finds
  zero branch targets, literal loads, ADR/ADDW materialisations, aligned 32-bit values or movw/movt
  pairs pointing into the gap [V];
* **What it changes.** The boot-task success epilogue becomes a poll of mailbox out[0]
  (CA `0x40039010`, read-only in the stub) that calls the firmware's own wrapper CPU `0x40294` ->
  dispatcher CPU `0xC18AC`; the dispatcher acks (`out[5]=1`), clears out[0], writes the H2D-done
  doorbell (`out[2]=8`) and dispatches the pending bit through the instance handler table. Every
  device write is the firmware's own; our host "never write CA `0x400392F0`" rule is unchanged
  [V]; the boot page/state/mask and the bench-validated duplex lap are untouched [V*];
* **H1 settled (2026-11-25 21:30; was the audit's open question).** The decisive five-word
  read-only pack was taken on a PD4 golden boot 2 s and 48 s after release: CPU `0x10C1B0` =
  `0x00118D68` (the image's static value), the live table at CPU `0x118D68` is **all zero** at
  both reads, the ROM RAZ window `0x2000` = 0, and the sanity anchor CPU `0x40000` = `0x00046971`
  (image loaded correctly). Interpretation: the sentinel compare is always equal, the stub always
  substitutes the ROM zero table, and **the firmware's mailbox dispatcher handlers never execute
  under PD4**; the working duplex is the non-mailbox ETE/polled H2D delivery, with the mailbox
  ack/clear/doorbell side effects as bookkeeping. This refutes the earlier lane assumption that
  PD4 dispatches through the real vendor handler table (correction C29) [V*]/[I];
* **Variants ranked.** `PD4-NOSENT` (force live branch, sha256 `0bc1b3e8...`) and
  `PD4-ALWAYS0` (force consume-only, sha256 `84e9cd95...`) are built and hashed;
  `PD4-MIN` (36-byte delta) and `PD4-LITE` (44-byte) are specified; the one-shot epilogue-call
  variant is rejected (the host posts frames after boot) [V];
* **Vendor-clean alternatives.** No host-only path is proven. The vendor mailbox sequence is
  already reproduced; the vendor's deferred-send additionally writes `out[5]=8` pre-kick and was
  tried once in the takeover era (accepted, no frame accept); ETE 0x2D/0x2E do not deliver, the ROM
  has no mailbox service, the image has no other out[0] reader, and CA `0x40160000` is outside the
  host-visible device-IO region [V]/[V*];
* **The one remaining host-only candidate** is a spare inbound iATU viewport onto device CA
  `0x40160000` plus a one-shot raise of event 0x4C (CA `0x40161188 = 0x1000`, mirror
  `0x40161288`; per-event enable already set at CA `0x40161108` bit 12). This is a new
  device-write class (firmware-owned interrupt controller) and has not been attempted; it needs its
  own attended experiment, recovery card and IRQ-storm/one-shot bounds [V]/[I]/[U].

**Verdict.** Firmware patching is required for the closed vendor blob **as delivered** on this
board; it is conditional only on the untested event-controller aperture experiment and is not a
fundamental property of the hardware. PD4 stays a **research instrument only, never a shipping
change**, and is published as a patch recipe plus verifier, not as vendor firmware bytes (section
11). The audit also corrects two older lane statements (section 8, C27/C28).

### 6.7 The real firmware management frames and the VAP-start wall (2026-11-26)

**Why this section exists.** v2 ended with the cfg80211 wiphy/VAP netdev lifecycle (M0/M1) and the
open user/vap-add and scan payload structs. The 2026-11-26 sessions sent those messages to the
firmware for the first time; finding F11 carries the analysis, this section the operational record.

**Frames.** Three management frames were built by the provider (module 676,180 B sha256
`4a1c3b9a...`, caps `0x1f`), passed the fail-closed gate and were consumed by the firmware with
zero exceptions, on the standard cfg80211 bench image with the C2 ladder in the same boot. The
owning lane later rebuilt the provider with the scan-D2H decoder integration (**704,720 B sha256
`20d38c5c...`**, `wifi-scan-ap/BUILD-NOTES.md`); the 676,180 B build is the historical fired
artifact, recorded in the ledger and in T27:

| # | frame | wire | builder | SR node word1 | post result | firmware result |
| ---: | --- | ---: | --- | --- | --- | --- |
| 1 | `user_add` g0 id7 | 180 B | `hmac_user_add` (168 B payload; uid, MAC, type 2, rssi) | `0x00b46d2b` | SR0 slot 2 devva `0x8417c000`, producer 2->3, consumed 0 polls, posted 1/8 | VAP committed (`user_id=0 vap_id=0`) |
| 2 | `user_add_notify` g0 id8 | 180 B | `hmac_user_add_notify_alg` (uid, alg, VHT/HT/HE caps, nss) | `0x00b46d2b` | posted 2/8, consumed 0 polls (bounded announce wait) | accepted (no exception) |
| 3 | `scan_req` g0 id0xC | 520 B | `hmac_enc_scan_send_req` (508 B payload: flags/BSSID/8 SSID slots/42 channel entries/dwell 60) | `0x02086d2b` | SR0 slot 4 devva `0x8371f000`, producer 4->5, consumed 0 polls, posted 3/8 | accepted (no exception); **no scan events** |

Table T27 has the full frame map and the un-pinned fields. The VAP del builder (`g0 id9`, 24 B) is
byte-exact from the shipped del_user attr but was not needed in these sessions [V].

**The scan wall (lifted in v4).** After the scan request the driver waited its bounded 5000 ms for
the D2H scan events (`g1 15/16/17`, `g2 2`), received none, and completed the cfg80211 scan as aborted
(`scan DONE (finish-abort, results=0 ... timeout=1) -> cfg80211_scan_done(aborted=1)`; `iw`
reported "scan aborted!"). The direct-read catalogue contained only the boot-time unsolicited
`g1 id3` (36 B), so the firmware had not sent result frames at all. The decoded firmware scan
handler (CA `0x6494C`) reads **VAP id 0**, and our `g0 id7/id8` only create the mailbox-layer
user/VAP; the per-radio VAP object must be in the started state, which is the still-open
create/start family (vendor order: user/vap add -> start -> scan/AP; candidates: WAL group-2 AP
start, `g0 0xF/0x10` start_sta) [V]/[U]. This is the v3 wall (section 10.2 Q1), not a builder
defect: every frame the host posted was accepted and consumed.

**State after the session.** Golden page `0x86807FEF/7/2/0xABCD` intact, state 1, mask 0, staging
0, heartbeats continuing, no exception; the box stayed on the cfg80211 bench image with the stock
pair preserved [V*].

**v4 resolution.** The wall was not a builder defect: the mode-0 WAL group-2 VAP frames were
consumed but did not create the firmware VAP object; the **mode-1** carrier (`g0 id1` keys
`0x0064`/`0x0065`) does, and the firmware then produced real scan results (section 6.9, correction
C34). The same precondition later gated the AP start (F12/6.11).

### 6.8 One command reproduces the whole bring-up matrix (2026-11-26, final run 17:00)

The wave-3 bench-tooling lane turned the phase ladders into a single fail-closed command. The real
run

    phase_matrix.py --phase all --reboot-between --yes-really --attended

returned, in one session and with bounded reboots between phases. v3 reported the v3-era run
(05:30) where D2 was BLOCKED-DRIVER; the mode-1 VAP provider then produced real scan results, and
the final v4 run is **all-PASS**:

| phase | what it covers | result | checks |
| --- | --- | --- | ---: |
| boot | read-only golden page + ready handshake | PASS | 15/15 |
| C2 | six-calibration boot (create + TXQ + six 298 B messages) | PASS | 54/54 |
| D1 | cfg80211 wiphy + VAP netdev add/del via iw | PASS | 21/21 |
| D2 | scan with the mode-1 VAP provider: iw rc=0, dmesg channel results, results>0 | **PASS** (22-step/48-check D2 TRUE PASS ladder; 2 writes) | session PASS / protocol verdict PASS |
| E1 | MAC data-plane setup + TXQ-table address sync | PASS | 37/37 |

Evidence: `build/wifi-bench-phase-matrix/20261124T094310Z/` (first D2 TRUE PASS) and
`build/wifi-bench-phase-matrix/20261124T094716Z/` (the one-command all-PASS run; PHASE-MATRIX.md
records session result PASS + protocol verdict PASS for every row). The orchestration that made it
possible (commit `0394f80`): reboot-and-wait issues the immediate `/sbin/reboot -f` in the
background, clamps **every** transport call to the remaining budget and logs `[reboot]` progress
(observed: box back after 61.5 s, uptime 46 s, `seen_down=1`), owns the between-phase reboot,
aborts and marks the remaining phases NOT-RUN on failure, and runs each phase child in a killable
process group under a hard `--phase-timeout`; the device-free selftest grew to **563/563** in v4
(was 503 in v3) [V]. A D2 row is never faked: the runner classifies a documented pending-feature
outcome from the session evidence (`blocked_rc`/`blocked_patterns`, rc 95/161, "Not supported")
as BLOCKED-DRIVER instead of FAIL/ERROR when results=0, and the row went PASS in the final runs
because the firmware actually produced results - not because the classification was relaxed. This
### 6.9 The mode-1 VAP base: first real scan results and the full BSS decode (2026-11-26)

**Why this section exists.** v3 ended on the scan wall (6.7): the firmware accepted the VAP-create
and scan-request frames but produced no results. The wave after it found the missing object, got
the first real scan results, decoded them, and flipped D2 to PASS. This section is the operational
record; finding F12 covers the same precondition for the AP path.

**Two VAP-start families.** The vendor userspace has two equivalent-looking carriers, and only one
creates the firmware object the handlers need [V]:

* **mode 0 / WAL group-2 ("vendor order")**: cmd `0x0064` (VAP add, 56 B frame) + cmd `0x0065`
  (VAP start, 44 B) - the vendor host's `wal_hipriv_add_vap`/`wal_start_vap` shape.
* **mode 1 / `g0 id1` static keys**: key `0x0064` (VAP add, 48 B) + key `0x0065` (VAP start,
  40 B) - the MAC-layer carrier dispatched through config-key table A (rows 1/2 -> workers
  `0x449E4`/`0x46394`). This is the same carrier the bench-proven C2 frames ride.

The 2026-11-26 13:00 fire posted the full mode-0 ladder (user_add, notify, WAL `0x64`, WAL `0x65`,
scan_req): all five frames were consumed in 0 polls with mask 0 - and the scan still returned
nothing. The read-only readback showed the module-0 context ($\rightarrow$ the VAP object) still
zero: **the WAL group-2 frames did not create the firmware VAP object** [V*]/[I]. The lane's fix
order was mode-1 next: post `g0 id1` key `0x0064` then `0x0065` before the scan
(`vap_start_mode=1`), then user_add -> notify -> scan_req.

**The first real scan results (14:00).** Fired with the rebuilt provider module (debug-stripped
738,112 B sha256 `e09dc697...`; params `allow_frames=12 vap_start_first=1 vap_start_mode=1
vap_start_module=0 frame_gap_ms=100 h2d_send_budget=8`) on a fresh PD4 golden boot [V*]:

1. boot golden, C2 ladder clean (ctx + TXQ consumed in 0 polls);
2. `iw phy phy0 interface add wlan0 type managed` -> user_add `g0 id7` 180 B + notify `g0 id8`
   180 B consumed;
3. `iw dev wlan0 scan` -> the provider posted **vap_add `g0 id1` 48 B (3/12)**, **vap_start
   `g0 id1` 40 B (4/12)** and scan_req `g0 id0xC` 520 B (5/12), each consumed in 0 polls;
4. the firmware answered: dmesg `cfg80211: scan channel result slot=0 ch=1 (2412 MHz)` .. slot=5
   ch=6 (2437 MHz); `rd_catalog` showed **`g1 id17` (17) count=6 (124 B channel results) +
   `g1 id15` (15) count=6 (509 B beacons)**; the scan completed `finish-abort` after 6 of 13
   channels because the host bound was 5 s; mask 0, page/state golden, no exception [V*].

That is the project's first real firmware scan result: the mode-1 VAP base removed the object
precondition, the firmware scan engine ran, and the results were decoded into cfg80211 BSS
entries.

**The full BSS decode (16:30).** The scan-complete lane (module rebuilt from the mode-1 provider
base, **748,888 B / 750,512 B builds** sha256 `b42ff4ec...`) added three things [V]/[V*]:

* an **auto scan window** `clamp(3000 + n*900, 1000, 45000) ms` (14,700 ms for 13 channels) and a
  `scan_timeout_ms` override, plus a `scan_dwell_ms` sweep knob (vendor dwell 60);
* a **host-side completion rule**: success (`finish-ok[channel-coverage]`) when every requested
  channel was reported by `g1 id17` and a 500 ms settle window passed; a firmware completion
  (`g1 id16` status 1 / `g2 id2`) still wins immediately; a timeout is `finish-abort[host-timeout]`;
* a **full beacon IE walk** of `g1 id15` (802.11 body + 20-byte trailer): SSID/hidden, DS, HT
  cap/op, VHT, RSN, WMM, country, extended caps, ESS/privacy bits, channel from DS/HT-op, validated
  IE chain -> `cfg80211_inform_bss()` with a per-BSS dmesg line.

The 13-channel fire decoded **12 BSS entries** (the first full BSS decode) but still ended
`finish-abort[host-timeout]`: the firmware's real pace is ~3.5 s/channel, slower than the ~830 ms/ch
estimate, so the auto window was exhausted after 4/13 channels [V*]. The D2 ladder was fixed
against the *functional* gate instead: scan results > 0, iw rc=0, clean page/mask - which is what
the D2 TRUE PASS row records (6.8).

| item | value | evidence |
| --- | --- | --- |
| mode-0 result | 5/5 frames consumed (incl. WAL `0x64`/`0x65`), scan results=0, module-0 VAP object zero | ledger 2026-11-26 13:00 | [V*] |
| mode-1 frames | vap_add `g0 id1` 48 B -> vap_start `g0 id1` 40 B, both consumed in 0 polls | ledger 2026-11-26 14:00; `wifi-vap-start` | [V*] |
| first results | 6 x 124 B `g1 id17` (ch 1-6) + 6 x 509 B `g1 id15` within 5 s; results=6 bss=6 complete=0 timeout=1 | ledger 2026-11-26 14:00; `wifi-scan-complete` | [V*] |
| full decode | 12 BSS entries (SSID/DS/HT/RSN/WMM/country) from `g1 id15`; 4/13 channels at ~3.5 s/ch -> abort | ledger 2026-11-26 16:30 | [V*] |
| scan completion | host coverage rule + 500 ms settle; `finish-ok[channel-coverage]` / `finish-ok[complete-event]`; firmware failure -> abort | `wifi-scan-complete/REPORT.md` sec 3 | [V] |
| D2 ladder | 22 steps / 48 checks / 2 writes; PASS gates iw rc=0 + dmesg channel result + results>0 + mask/staging 0 + page golden | ledger 2026-11-26 15:00; T28 | [V*] |

**Limits.** No `finish-ok` 13/13 scan is in the record; the 12-BSS run ended on the host timeout.
Signal is 0 mBm by default (the trailer RSSI bytes are [U]); the 20-byte trailer semantics and the
`g1 id16`/`g2 id2` status code space are partially open. A full-channel scan and an `iw` BSS
listing in a passing D2 run remain follow-ups, not claimed results.

### 6.10 The hostapd capability ladder: from AP-DISABLED to AP-ENABLED (2026-11-26/27)

**Why this section exists.** With the AP-start sequence reversed (four-message AP config, 156 B body
mapped) and the frames built, the trigger was hostapd's `NL80211_CMD_NEW_BEACON` -> cfg80211
`.start_ap`. Getting hostapd to that point took five failed fires and seven distinct driver-side
capability gates; F13 has the analysis, this section the operational record and the gate table
(T30).

**Starting point.** The bench image had no hostapd. The hostapd-deploy lane built the OpenWrt
hostapd-mini v2.12 package set (with the ucode dependencies), host-verified it and wrote a
zero-flash T0 deploy path; that lane never contacted the router. The Lead deployed/ran hostapd in
the attended AP sessions.

**The gate sequence, in the order the bench found it** [V*]:

1. **ucode** - "Error loading ucode: Unable to resolve path for module 'common'": the image lacks
   `/usr/share/hostap/*.uc` (wifi-scripts). Fixed for the bench by pushing the three files; the
   real image fix is `CONFIG_PACKAGE_wifi-scripts=y`.
2. **CONNECT advertisement** - "nl80211: Driver does not support authentication/association or
   connect commands": hostapd's shared init gate refuses a driver advertising neither
   AUTHENTICATE nor CONNECT. Fixed with a fail-closed `.connect` (v2).
3. **SET_INTERFACE** - the M1 op refused/stored wrongly on type changes; v3 stores `wdev->iftype`
   and treats `use_4addr=-1` as unspecified; a VAP created as `type __ap` makes hostapd's
   SET_INTERFACE(AP) a same-type no-op.
4. **`.mgmt_tx`** - hostapd's AP frame subscriptions (`NL80211_CMD_REGISTER_FRAME`) are rejected by
   cfg80211 unless the driver has a mgmt_tx op. Fixed with a fail-closed `.mgmt_tx` (v4).
5. **`mgmt_stypes`** - the subscriptions still need `wiphy->mgmt_stypes` with the AP rx bits.
   Fixed by advertising AP rx/tx `0x3c15` (v5); dmesg confirms "hostapd frame subscriptions
   accepted".
6. **`REPORTS_OBSS`** - "Failed to set beacon parameters" was not SET_BEACON: hostapd's userspace
   AP SME registers `NL80211_CMD_REGISTER_BEACONS` before it builds NEW_BEACON, and cfg80211
   refuses without `WIPHY_FLAG_REPORTS_OBSS`. Fixed by setting the flag (v6).
7. **connect/disconnect WARN** - the `wiphy_new_nm` warning at `core.c:532` is
   `WARN_ON(ops->connect && !ops->disconnect)`; v6 pairs the gate with a fail-closed
   `.disconnect`.
8. **the firmware VAP base** - the last gate was F12: the AP frames need the mode-1 VAP object.
   v7 posts `g0 id1` `0x0064` 48 B -> `0x0065` 40 B once per load before the AP sequence.

**Interplay with the rest of the report.** None of these fixes changes a wire byte: the AP frames
were already byte-exact (T29); the gates are cfg80211 advertisement requirements and one firmware
precondition. That is why the same hostapd command flow that failed five times printed
`AP-ENABLED` after the v7 module (6.11).

**Host-side verification (no device).** `deploy-hostapd.sh --check` 88/88, `tools/selftest.sh`
14/14, `tools/check-ladder.py` 107/107; the config fixtures (OPEN ch 6, WPA2 negative control) parse
with a hostapd v2.12 build from the tree's own source, and the negative config is refused. The
device-side deploy preflight is the authoritative gate for the running image.

**Limits.** The gates are advertisements, not implementations: management TX/RX, OBSS reporting,
beacon control and WPA2 `add_key` remain absent and fail-closed. `change_beacon` accepts
SET_BEACON host-side with no firmware write. hostapd-mini does not autostart from `apk add` (no
`/etc/rc.d` link), which is deliberate for the bench.

### 6.11 THE FIRST LIVE WI-FI AP ON OUR CLEAN-ROOM STACK (2026-11-27 04:30)

**Why this section exists.** This is the v4 headline: hostapd reached the driver's `.start_ap`,
the firmware consumed the four-message AP start, and hostapd reported `wlan0: AP-ENABLED` with
SSID `cudy-ap-test`. F12/F14 carry the analysis and the honest boundary; this section is the
session record and the claim rules.

**Session shape.** Fresh PD4 golden boot (the previous attempt had crashed the firmware - see
F12), the v7 AP-start module **206,708 B sha256
`fd8e1439c7c323eae57b291fd034a647815ba5ab1ca27bedd48f83b3f9a45020`**, module params
`allow_frames=8 vap_start_first=1 vap_start_mode=1 vap_start_module=0 frame_gap_ms=100
h2d_send_budget=8`, then the proven C2 ladder, `iw reg set US`, `iw phy phy0 interface add wlan0
type __ap`, one bounded scan for the mode-1 VAP base, and hostapd-mini 2.12 with the OPEN ch 6
config (beacon_int 100, DTIM 2).

**The observed sequence** (dmesg / hostapd) [V*]:

```
cfg80211: REPORTS_OBSS flag set (hostapd wiphy-data AP registration)
cfg80211: mgmt_stypes advertised (AP rx=0x3c15 tx=0x3c15) - hostapd frame subscriptions accepted
cfg80211: start_ap reached (iftype=3 beacon_interval=100 dtim=2 head=63 tail=9) - provider gate next
frame post vap_add:   g0 id1 total 48 B  ... posted 3/8, consumed 0 polls   [mode-1 VAP object]
frame post vap_start: g0 id1 total 40 B  ... posted 4/8, consumed 0 polls
frame post ap_ssid:   g0 id1 total 64 B  ... posted 5/8, consumed 0 polls   [key 0x0005]
frame post ap_cfg:    g0 id1 total 184 B ... posted 6/8, consumed 0 polls   [key 0x0127]
cfg80211: start_ap sequence posted to provider vapscan-0e4ad45
hostapd: wlan0: interface state UNINITIALIZED->ENABLED
hostapd: wlan0: AP-ENABLED
iw dev wlan0 info -> type AP, ssid cudy-ap-test
```

**Readback** (read-only, after the sequence) [V*]: golden page `0x86807FEF/7/2/0xABCD`, state 1,
mask 0, staging 0; `ctx0+0x18C` changed to `0x70094810`; VAP pointer `ctx0+0x190` =
`0x01010F18` nonzero - the firmware AP object exists. No exception was raised; every AP-start
frame was consumed on the first bounded poll. The VAP `+0x10 = 100` / `+0x14 = 2` dereference
window remains [I] and was not read in this record; the PASS does not depend on it.

**Claim rules.** A consumed AP-start sequence plus a clean firmware readback is the control-plane
evidence. hostapd `AP-ENABLED` is a host-side fact; beaconing over the air is not observable from
the same radio, no client associated, and the data plane is POST-ONLY (6.12). Never quote this
milestone as "a working AP" without those limits; T29 has the exact tables.

**v5 update (2026-11-28).** Two things changed after this session: the AP-start order was shown to be
**five** messages with the channel config first and the VAP start after the AP config (section 6.15,
F17) - the re-fire with that order also reached `AP-ENABLED` - and the WPA2 variant of the same AP
installed its GTK (section 6.14, F16). The AP also restarts in one boot (E2) and survived a
40-minute soak with **242/242 samples golden** (section 6.11 evidence chain; ledger 2026-11-28 13:30).

**On the flashed image (2026-11-27 06:30).** The milestone was then **reproduced on the delivered
image**: the ABI-correct container (`df6a78c3...`) was flashed with the stock-assisted installer
(kernel read-back matched slot `1794a531...`, `ubiformat` OK, active pair untouched; the tool's env
step errored but the environment already read `bootflag=b`, and the handover script set
`bootflag=b` + pristine `wifi_init` + `boot_reg=0x21`), the box rebooted into 6.18.60 from
`ubi.mtd=rootfsb` with selector `0x11`, and the post-boot ABI receipt matched:
`apk query --fields version kernel` = `6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1` = the kmod pin
- **the kmod-ABI mismatch is closed**. Native `modprobe cfg80211` and `modprobe hi5622_wifi` returned
rc=0 from `/lib/modules/6.18.60` (no `/tmp`), native `iw` 6.17 + `iwinfo` work, and the T0 full-set
verify passed **38 gates** (only a cosmetic "receipt missing" because the flash wiped the overlay).
With hostapd plus the ucode files, the PD4 firmware and the v7 ap-start driver, the same flow gave
`AP-ENABLED` again (hostapd rc=0, four hostapd processes, `type AP, ssid cudy-ap-test`; all four
AP-start frames consumed in 0 polls; page `0x86807FEF` golden, mask 0) [V*]. Minutes later the AP
was still live (hostapd processes alive, same SSID/type, no exceptions) and the D2H catalogue had
gained a family absent at boot: **`g1 id0x23` (35) count=6, 24 B** (plus `g1 id3` count=20, 466 B
responses). The new family is likely AP/beacon-period firmware activity [I]; it is the first
firmware-side sign that the AP start changed ongoing behaviour, but the on-air beacon still needs an
external observer and the family's meaning remains [U].

**What made it possible.** The whole chain, each link previously hardware-validated: clean-room
PCIe transport -> golden boot -> HCC duplex -> calibration C2 -> cfg80211 wiphy/VAP -> mode-1 VAP
create -> AP config; the v4 additions are the four AP messages, the capability gates, the firmware
VAP precondition, and the ABI-correct image flash that closed the delivery caveat.

### 6.12 The first data-plane frame on the MAC TX ring (POST-ONLY, 2026-11-27 02:30)

**Why this section exists.** E1 (6.4) programmed the MAC slot-5 rings and synced the TXQ table, but
no frame had traversed them. The dp-air lane merged that data plane into the hardware-validated
mode-1 provider and delivered the F-AIR ladder; the first real fire put a frame on the ring. F14
and correction C41 carry the honesty rules; this section is the record and the ladder map (T31).

**Merged artifact.** `hi5622_wifi.ko` **886,864 B sha256
`ca32aec46dae46de536281ee4dee8d8255d6981570b61bd86b186c50c6d5b4a0`** (revision 2): the
provider tree `wifi-scan-complete/src` (748,888 B `b42ff4ec...`) plus the E1 data-plane objects and
the wifi-dp-txrx portable TX/RX logic; merge provenance enforced by the lane's verifier; **1,483
device-free host checks** (242 H2D + 254 txrx + 290 scan + 195 scan-complete + 454 cfg80211 + 48
merge-specific), 0 compiler/modpost warnings, vermagic 6.18.60; the F-AIR sequence
`bringup-f-air-txrx.json` is 30 steps / 83 checks / 7 writes (sha256 `96354365...`).

**First fire (00:30) - fail-closed by design.** The ladder stopped at `dp_probe` because the
operator wrote the attribute instead of reading it: `dp_probe` is a **READ** action (one read-only
MAC word read); a write returns rc=1, performs no read and leaves `probe_ok=0`, so `dp_setup`
and every later action refused. This was a documentation/attribute-mode bug, not a device fault
(correction C40); revision 2 accepts both `cat dp_probe` and `echo 1 > dp_probe` as the *same
read*, and the E1 doc's old `echo 1 >` spelling is retired.

**Second fire (02:30) - the first frame** [V*]:

1. boot ladder + C2 clean; mode-1 VAP + scan precondition in the same load;
2. `cat dp_probe` -> `probe_ok=1 probe_value=0x00000020` (the readable MAC band-0 word);
3. `echo 1 > dp_setup` -> rings 256/512/4095 (4-byte entries), TXQ table `0x2524` devva
   `0x835d4000`, tx rings `8x64x8`, tx_ba_info `0x800` devva `0x84251000`, MSDU table 8 entries
   at CA `0x01060850`/ACP `0x02060850`, MAC band 0 14 regs written+verified (mask `0xfffffcc1`);
4. `echo 1 > h2d_txq_addr_sync` -> `g0 id1` key `0x0320` sent with TXQ-table devVA
   `0x835d4000` (9508 B table, 32 B frame); firmware worker CA `0x81445` stores it;
   `h2d_status ok=3 txq_addr_sent=1 timeout=0`;
5. `echo 1 > dp_tx_frame` -> `DP tx_frame: type=1 fc=0x0040 mpdu=32 B buf_devva=0x84565004
   ring[tid=0 idx=0]={buf=0x84565004 ctl=0x00108020} wptr 0x0000->0x0001 table@CA 0x01060850=
   {ring=0x835d7000 wptr=0x00000001 rptr=0x00000000} completion_pending=1`;
6. `dp_tx_status`: `bound=1 frame_ready=1 posted=1`, `wptr=0x0001`,
   **completion_pending=1 = POST-ONLY**; `dp_rx_poll`: 0 captures (no started radio/peer at TX
   time); post pack: mask 0, staging 0, page/state golden.

**Interpretation.** The host -> coherent frame buffer -> 8-byte MSDU ring entry -> device-RAM
12-byte table publish -> read-back chain is proven. The MAC did not consume the entry in that
session (`completion_pending=1`), so the strongest verdict is POST-ONLY. `COMPLETED (MAC)` requires
`completion_pending=0`; even then a broadcast probe request has no ACK and the clean-room host can
only prove the MAC-completion chain - an **external observer** is required for any on-air claim,
so the ladder's best honest label is air-CANDIDATE. The next fire can build on the first live AP:
the radio/VAP are now up, and the merged module already carries the mode-1 provider, so a TX re-fire
has the precondition the 02:30 session had to approximate.

**Limits.** No TX completion, no RX capture, no observer, no data-class frame discrimination at this
cut-off (optional `echo 2 > dp_tx_frame` posts a null-data at slot 1; the pair would settle the [I]
ctl vap-type mapping). One boot = one setup + one 0x0320 sync + at most two TX posts + one RX poll;
no retry after a failure.

**v5 update (2026-11-28 01:00): POST-ONLY is solved.** The dp-tuning lane found the missing
device-consumer step: after the ring push the vendor publishes the **TXQ-table queue state**
(`hmac_update_user_tid_txq_table`: per-TID count/start-SN/bytes, the user bitmap byte and a u16
timestamp). With that publish in the driver, a fired ladder on a fresh AP-ENABLED boot reported
`DP txq: published user=0 tid=0 count=1 bytes=32 sn=0 ...` then `DP tx_complete_poll: COMPLETED
(MAC) after 0 ms - device rptr 0x00000001 == published wptr 0x00000001` and
`table={ring, wptr=1, rptr=1}` [V*]. The host->ring->table->MAC consumer chain is therefore
closed; the remaining air-side proof is unchanged (external observer, no ACK for a broadcast probe
request). The report's earlier POST-ONLY verdict is superseded (correction C54, section 6.12/T31).

### 6.13 The MAC RX path: the wrong-block correction and the live-bank proof (2026-11-28)

**Why this section exists.** From v2 to v4 the report carried a single RX negative: the completion
ring never moved, so no frame was ever captured. The static-RE wave showed the negative was a
**telemetry bug**, not a dead data path: the host was polling the config block instead of the
live-bank block. The `dp_rx_arm` rung then proved the device consumer on hardware. F15 has the
analysis; this section the operational record and the map (T32).

**The two blocks** [V]:

| block | role | (band 0) |
| --- | --- | --- |
| CA 0x40040000 | MAC host-interface **config**: ring addresses/sizes, host-owned wptr/rptr, W1C status +0x44, mask +0x48 = 0xfffffcc1; the 14-write program's block | the `dp_setup` target |
| CA 0x40050000 | **live** host-MAC status + ring pointers: +0x00 status, +0x08 norm free-ring consumer rptr, +0x0c small consumer rptr, +0x10 completion producer wptr | read by `dp_rx_arm` |

The config words **+0x4c/+0x50/+0x54 are never read by the vendor** and stay 0 in the stock live
dump, while the live block reads norm/small rptr = **0x20** (the MAC prefetched 32 free buffers)
[V]/[V*]. The completion path: each completion entry is the **device VA of buffer+4**; the device
header is **36 B at buffer+4**, with payload length at header+0x1a (buffer+0x1e; gate
len+0x28 <= 0x640), next sub-MSDU at header+0x20 (buffer+0x24), flags/status at header+0x5/+0x7 and
the MPDU at **buffer+0x28** [V]. The inherited capture decoder read the length 4 bytes high - that
is corrected in T32 and in the lane patch.

**Enable model.** The vendor's RX init is a 12-write free/completion-ring program (two free rings
with prefilled `devVA(buf+4)` entries, then the completion ring); there is **no additional RX
enable bit and no H2D RX message** - the whole device enable is the ring program the E1 `dp_setup`
already performs. The missing half was always the **host consumer**: advance the free-ring
rptrs/refill from the completed queues bounded by the live rptr delta, publish the completion
consumer, and never re-publish an in-flight buffer [V].

**Implementation (fail-closed).** `rx_enable` (default **0**) gates everything; `dp_rx_arm` is a
read-only validation of the live bank (refuses on 0xffffffff/mask/bad-index, logs the live bank);
`dp_rx_drain` is the only pointer-moving action and validates every completion entry and header
before publishing (lane closes on any anomaly, no retry). The patch adds `hi5622_rx_live.{c,h}` +
glue on the final combined tree; host selftest **51/51**, module build 0 warnings, patch
round-trips byte-for-byte [V].

**Fired (2026-11-28 21:00).** Fresh golden boot + AP-ENABLED:

```
DP rx_arm: live bank CA 0x40050000 status=0x00000182 rx_pending=0
mask=0xfffffcc1 (vendor 0xfffffcc1) norm_rptr=0x0020 small_rptr=0x0020
cmp_wptr=0x0000 pending=0 | config: norm_wptr=0x8000 small_wptr=0x8000 cmp_rptr=0x0000
verdict - free-rptr movement proves the MAC consumed published free entries
(vendor stock mirror was norm/small rptr = 0x20 after a full fill)
```

`dp_rx_drain`: `no new completion (rx_pending=0 ...) - no pointer moved` - nothing received yet,
because the bench has no on-air traffic. Page golden, mask 0, staging 0, no exception [V*].

**Interpretation and limits.** RX is armed and its device consumer is proven; the earlier
"no RX" claim is superseded (corrections C46/C47). What remains is traffic: an external client (or
a host-generated frame that reaches the MAC) is needed for a capture. Sub-MSDU chains are [U] and
refused (`HIL_RX_ECHAIN`), netdev/MSDU delivery is not implemented (capture-only), and the mapping
of which frames the firmware forwards to the host rings is [U]. The feature is carried by the final
all-patch module and was re-validated in its one-boot fire (6.18). The table map is T32.

---

### 6.14 The WPA2 key protocol: GTK install on hardware (2026-11-28)

**Why this section exists.** v4 listed WPA2 `add_key` as M3/absent and therefore a usable-AP
blocker. The static wave showed the GTK install happens at AP start, before any client, and the
bench fire proved it. F16 has the analysis; this section the protocol map (T33).

**Protocol** [V]: the five cfg80211 key callbacks are all thin wrappers that each post **one**
group-0 id-1 config event on the same carrier as the bench-proven AP/TXQ frames:

| cfg80211 op | g0 cmd | body | firmware worker (table A) |
| --- | ---: | ---: | --- |
| add_key | **0x0078** | 0x44 B: key_index, pairwise, MAC[6], key[32], seq[16], key_len@0x38, seq_len@0x3c, cipher@0x40 | CA 0x0A5825 |
| set_default_key | **0x0079** | 0x28 B: key_index, unicast, multicast (and a class flag for mgmt) | CA 0x0A5C9D |
| del_key | **0x007A** | 8 B | CA 0x0A4271 |

The firmware classifies ciphers by the low byte of the cfg80211 suite (data class 0x0714, mgmt
class 0x3840, WEP class 0x0022), enforces the data/mgmt key-index split (data <=3, mgmt 4..5),
stores the key in the per-user cache (`user+0x310/0x311` data, `+0x312/+0x313` mgmt) via the
key-table manager at `module_ctx+0x4b8`, and copies key/seq bytes from the body - there is no
host-visible MAC-register programming step [V]. The 2.4.15 and 2.5.24 host modules are identical
for these ops; the vendor's own HMAC path (`hmac_user_add_key` -> `hdpp_config_send_event`) builds
the same frame, so the g0 carrier is not a clean-room guess. The patch registers
`.add_key`/`.del_key`/`.set_default_key` (cfg80211 6.18 requires all three together at
`wiphy_new` or it WARNs) behind **`wpa_keys=0`** fail-closed, with the three portable builders, provider caps and a status
line; key bytes are never logged [V].

**Fired (2026-11-28 23:00).** WPA2 hostapd AP, `wpa_keys=1`, fresh golden boot: `AP-ENABLED`, then
`frame post key_add: g0 id1 total 96 B ... consumed after 0 polls` (cfg80211: idx=1, pairwise=0,
cipher 0xfac04, class=data, key_len=16) and `frame post key_default: g0 id1 total 68 B ... consumed
after 0 polls` (idx=1, uni=0, multi=1); page golden, mask 0, staging 0, no exception [V*]. The GTK
(idx 1, CCMP, 16 B) is installed and selected as the default multicast key - the M3 unblock is
hardware-proven. The 4-way PTK needs a client and the RX path (6.13), so it remains open.

**Limits.** Only the GTK/OPEN-WPA2-PSK path is exercised; `set_default_mgmt_key` is deliberately
unregistered (the vendor builder leaves a cipher byte the firmware mgmt path wants), the `get_key`
reply layout is [U], and no client/PTK run exists. The feature is carried by the final all-patch
module and was re-validated in its one-boot fire (6.18). The protocol map is T33.

---

### 6.15 The five-message vendor AP-start order (2026-11-28)

**Why this section exists.** v4's AP start was four messages, with the mode-1 VAP base posted
before the AP config and no channel configuration at all. The AP reached `AP-ENABLED` but no
on-air beacon was ever observed. The static wave found the vendor's real order and the bench fire
reproduced it. F17 has the analysis; this section the ordered map (T34).

**Vendor order** [V] (both 2.5.24 and 2.4.15 host modules):

1. **channel config** - WAL cfg command **0x0124**, 4-byte body {band, vendor per-VAP byte,
   channel, unused}; firmware worker CA 0x841B8 stores band/channel/valid at module ctx
   +0x11/+0x10/+0x13 and runs the PHY channel program. This is the **first** call in
   `wal_cfg80211_start_ap`.
2. **set SSID** - WAL cmd **0x0005**, 36-byte body (g0 key 0x0005; 64 B g0 frame).
3. **auth** - WAL cmd **0x006D**, 4-byte body; **WAL envelope only** (no static g0 key in table A).
4. **AP config** - WAL cmd **0x0127**, 156-byte body (g0 key 0x0127; 184 B g0 frame).
5. **VAP start** - WAL cmd **0x0065**, 12-byte body, posted **after** the AP config by
   `wal_cfg80211_start_ap_write_msg`.

**Beacons are firmware-owned.** The host does not post a beacon template or a bcn-ring message:
the firmware image carries the SMAC TBTT ISRs and beacon suspend/resume handlers, and the frame is
assembled at runtime from the VAP context; the AP-config body carries only parameters (beacon
interval, DTIM, RSN/vendor-IE copies, HT/VHT/mode, hidden flag, mobility domain) [V]. The
clean-room gap was therefore the missing channel step and the VAP-start ordering - exactly what
the patch fixes: `beacon_static=0` (default, old path unchanged) / `beacon_static=1` (vendor order),
a new `CHAN_CFG` capability and channel builder, and a pure ordered plan
`user_add -> channel -> SSID -> ap_cfg -> VAP start` with a module-ctx channel readback line
(log-only) [V].

**Fired (2026-11-28 22:00).** A first build's fresh-post guard used `module_ctx+0x190` as a
VAP-validity indicator and false-negatived (the field is asynchronous and also reads zero on a
successful AP start; correction C53). After the guard fix the fire executed the exact vendor order:
`plan (5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start`, `ap_chan posted (32 B, band=0
channel=6) - the firmware module channel context is programmed BEFORE the AP config`, then SSID,
`ap_cfg`, `ap_vap_start posted after ap_cfg`; hostapd `AP-ENABLED`, ssid `cudy-ap-test`, type AP;
page golden, mask 0, staging 0 [V*].

**Limits.** On-air beacons still need an external observer; the AP-side auth/assoc management
path (`wal_cfg80211_mgmt_tx`) is not implemented in the clean-room driver (fail-closed), so a
usable AP for clients is a separate, larger milestone. The feature is carried by the final all-patch
module and was re-validated in its one-boot fire (6.18). The ordered map is T34.

---

### 6.16 The full-13-channel scan ceiling: a structurally closed question (2026-11-28)

**Why this section exists.** The scan line had produced real results and a measured 7-8 channel
ceiling with the H1 band-config message, but the hope of reaching 13/13 by programming the g0 id10
per-slot array remained. The static wave closed that question as an evidence-backed negative. F18
has the analysis; this section the operational record (T35).

**What the message is** [V]: the g0 id10 handler CA 0x5FF64 decodes a **32-byte payload / 44-byte
frame** relay of received **ADDBA response / DELBA** management frames (the only two vendor
builders: `hmac_mgmt_rx_addba_rsp` type 3/subtype 1 and `hmac_mgmt_rx_delba` type 3/subtype 2).
Subtype 1 carries object key, slot 0..7, action, start/step/mode/counter fields; subtype 2 is the
clear path. The per-slot context array is `obj+0xEC+slot*0x20` (8 slots) - but the scan engine
(`[[module+0x4B8]+0x1A00]`) reads the slot-0 context only (0xFC start, 0xFD step, 0xFE end/count,
0x103 mode, 0x104/0x105 result counters, 0x106 stride default 6), and **no subtype-1 field writes
the count/end byte +0xFE**; only the subtype-2 clear path writes it. Programming slots 1..7 is
therefore inert for the scan count (the sibling g0 id11 handler CA 0x5E2F4 is the one that writes
per-slot channel bindings [U]).

**The measured ceiling** [V*]: 6 channels/request without H1 (the firmware advances quickly through
6 and hard-stops - the earlier "3.5 s/channel" reading was the host window, not firmware pace);
7 channels with the H1 pacing config (8 with a larger result-counter preset but results=0); mode=1
gives a dense/passive 4-channel/14-BSS yield. 13/13 in one request is not reachable; the shipped
fallback is host batching (6+6+1). The H1 message is **pacing**, not a channel count (payload+0x10
presets the result counter +0x104, payload+0x12 sets mode +0x103) [V].

**Corrections folded in:** dispatch row CA **0x10CD5C** (table 0x10CCFC; 0x10CD08 is id 1), vendor
wire shape **44 B** (32-byte payload, not 36), subtype-2 flag at **payload+0x14** (not +0x08), and
the H1 builder default `scan_band_cfg=0` fail-closed [V]. The ceiling map is T35.

**Why this matters.** It prevents more bench fires chasing an impossible 13/13 and it fixes the
research record: the g0 id10 family was misread as a channel-plan message; the 13-channel limit is
a firmware-owner design choice (slot 0 only, stride default 6), not a host bug. The final all-patch
module keeps `scan_band_cfg=0` (H1 off by default), consistent with this closure.

---

### 6.17 STA, 5 GHz and HT/VHT: the static model and its limits (2026-11-28)

**Why this section exists.** v4's driver had `-EOPNOTSUPP` STA connect stubs, a one-wiphy-per-PCI-
endpoint model and only HT20. The static wave decoded the real STA/band/HT model and delivered a
fail-closed patch. F19 has the analysis; this section the decoded tables (T36).

**STA connect/disconnect** [V]: a **WAL cfg-event** (group 2 / HCC id 0 / WAL id 1 - the same
envelope as the AP-start frames), opcode **0x0123** with a **108-byte body** (ssid_len, mfp, IE/
SSID/BSSID/key host-buffer addresses, privacy, auth_type, key_len/key_idx, ie_len) for connect and
opcode **0x00d0** with a **16-byte body** {bssid[6], reason u16, [15]=1} for disconnect. The
inherited allowlist row named `KICK_STA` at 0x00d0 is in fact cfg80211 disconnect (naming-only
correction). The vendor is a **connect-SME** driver: `.auth`/`.assoc` are NULL and the firmware
performs the authentication/association; the completion is a D2H management event that our RX path
does not decode, so the patch posts the join but **never fakes `cfg80211_connect_done`** [V].

**5 GHz** [V]: one wiphy **per band**, not per PCI endpoint - `wal_cfg80211_init` loops over the
board band list and creates one wiphy per band on chip 0 (two RF chips behind one host MAC), which
is why stock shows two wiphys. The domain->band mapping of our endpoint model stays [I] (the board
INI is vendor userspace). Vendor band tables: 2.4 GHz **14 channels** (ch1-14), 5 GHz **29
channels** (36-64, 100-144, 149-165, plus the 4.9 GHz public-safety 184-196 block); 2.4 GHz has 12
rates, 5 GHz 8; per-channel `max_power` template 30 (live 23/24 dBm after the regulatory pass).

**HT/VHT** [V]: HT cap **0x0062** = SUP_WIDTH_20_40 | SGI_20 | SGI_40 (both bands); VHT advertised
on both bands, **2 SS MCS0-9** (`0xfffa`); VHT cap **0x00400000** (2.4 GHz) / **0x00400064**
(5 GHz = HTC_VHT | 160 MHz | SGI80 | SGI160) - matching the live ch44 @160 MHz readout. The static
HT template's MCS rx_mask is all zero (runtime/INI-filled [U]) and is deliberately not copied.
The patch adds `sta_join` (0 fail-closed / 1 dry-run / 2 post), `ht_mode` (0 baseline / 1 vendor
HT20 word / 2 HT40 `0x62`), `vht_cap` and `band_vendor`, plus the WAL allowlist {0x0123,108}; module
builds warning-free (stripped 278,044 B sha256 `333c72a5...`) [V].

**Interpretation and limits.** Capability advertisement is not a working client: HT40/VHT use needs
the firmware-side channel-width configuration and a STA also needs the D2H management completion,
RX/EAPOL and WPA keys. The patch **has not been fired as a standalone connect rung** at the report
cut-off (it is included in the final all-patch module with `sta_join=0`, so the join itself is
unexercised); its ladder rungs B0-B6 are designed, and the first controlled post carries the [U] risk
of the connect-body host-buffer dereference contract. The decoded tables are T36.

---

### 6.18 The final all-patch module: one boot, all features (2026-11-29 01:00)

**Why this section exists.** v5's five feature fires (6.13-6.17) were separate modules on separate
boots. The culminating integration question - do the RX consumer, the WPA2 GTK, the five-message
AP order, the completing TX ring and the scan/STA parameters all work **in one module on one
boot**? - was answered by the final all-patch module. F20 has the analysis; this section is the
operational record (T38).

**Module.** Chain `base -> image-polish d145a205 -> dp-tuning 175a4617 -> scan R4 -> stability
b5dfb883 -> rx-static -> beacon-static (+guard fix) -> wpa-static -> sta-static`, with six additive
conflict resolutions documented and a reproducible build script that refuses drift. Identity:
**stripped 308,080 B sha256 `5b587949...`, 47 parameters, 0 warnings, cfg80211 contract 10**
(opensource `008aca5`) [V].

**One-boot validation (fresh PD4 golden boot, full parameter set).**

| # | feature | observed | evidence |
| ---: | --- | --- | --- |
| 1 | AP, vendor five-message order | `vap_add -> ap_chan (32 B, band=0 channel=6) -> ap_ssid -> ap_cfg -> ap_vap_start`; hostapd `AP-ENABLED` (rc=0) with the WPA2 config, ssid `cudy-ap-test`, type AP | [V*] |
| 2 | WPA2-AP GTK | `key_add` 96 B (idx=1, cipher 0xfac04) + `set_default_key` 68 B (multi=1), consumed 0 polls, workers CA 0x0A5825/0x0A5C9D | [V*] |
| 3 | TX completion | `dp_tx_complete_poll verdict=COMPLETED`, TXQ table `{wptr=1, rptr=1}` | [V*] |
| 4 | RX arm | live bank CA 0x40050000 status 0x00000182, mask 0xfffffcc1, norm/small rptr 0x0020 (vendor mirror) | [V*] |
| 5 | post pack | page `0x86807FEF` golden, state 1, mask 0, staging 0, no exceptions | [V*] |

**Interpretation and limits.** The clean-room stack is feature-complete at the control/device-plane
level in a single module. Still outside this validation: the external-observer on-air proof
(beacon/association/4-way PTK), an RX capture with traffic, a fired STA join (`sta_join=0` kept the
connect unexercised), the 5 GHz band config, and baking the final module into the self-contained
image. The module chain and fire recipe are the record for those follow-ups. The validation matrix
is T38.

---

## 7. Bench evidence chain

Every hardware-verified claim in this report has an attended session behind it. This table is the
chronology; **the newest entry wins** over any older paragraph (project rule), and section 8 lists
the claims that a later entry superseded. Times are ledger timestamps (Asia/Bangkok); "ledger"
means `mem-entries.md` in the superproject. Sessions were attended and recovery-gated; no session
was ever run from this document.

| date / time | session | what was measured (claim) | evidence source |
| --- | --- | --- | --- |
| 2026-11-22 | transport A/B1/B2 | six-viewport iATU, contiguous 928,920 B upload with verify diffs=0, CPU release, polled D2H bitmap, ETE rings armed, H2D smoke | wifi-transport-port SUMMARY/REPORT [V*] |
| 2026-11-23 | recovery + B3 | box recovered to W27b; install-from-stock path validated; B3 cold-boot ready-gate test NEGATIVE (g=0, registry unpopulated) - motivated the firmware-internal investigation | ledger 2026-11-23; wifi-fw-ready-gate [V*] |
| 2026-11-24 01:15 | GP1 | SoC GPIO endpoint reset pulse makes ROM S0/S1 run; handshake page reached the contract values; pulse necessary but flaky (2/4) | ledger 2026-11-24 01:15 [V*] |
| 2026-11-24 02:20 | GP1 re-test | pre-upload chip state after the pulse is identical across boots; post-release page alternates between contract and RAM-fallback - "S1 condition-dependent" | ledger 2026-11-24 02:20 [V*] |
| 2026-11-24 03:10 | GP2 | hog removed, driver-owned pins: mask becomes 0 (no exceptions) but S1 never completes (page +8 = 0) | ledger 2026-11-24 03:10 [V*] |
| 2026-11-24 04:00 | GP3 | seeding the four TCM section bounds makes entry A copy the CPU1 ITCM/DTCM (word0/DTCM exact); S1 still absent | ledger 2026-11-24 04:00 [V*] |
| 2026-11-24 05:00 | stock golden read | working vendor boot: handshake page contract, TCM values, ROM status, **full 24-word platform block captured** (read-only) | ledger 2026-11-24 05:00 [V*] |
| 2026-11-24 05:40 | GP4 | full 24-word vendor platform block: TCM byte-identical incl. coreword 0x37C8F8D3; S1 still absent | ledger 2026-11-24 05:40 [V*] |
| 2026-11-24 06:20 | stock golden read | **618-word wifi-custom block captured** (read-only); host-pointer words noted as a blind-copy hazard | ledger 2026-11-24 06:20 [V*] |
| 2026-11-24 07:20 | stock kprobe | exact vendor pre-release order: firmware chunks -> platform block -> wifi block -> release 0x5A5A | ledger 2026-11-24 07:20 [V*] |
| 2026-11-24 08:00 | GP5 | both vendor custom blocks before release: same no-S1 result | ledger 2026-11-24 08:00 [V*] |
| 2026-11-24 08:45 | GP6 | vendor-matched pulse timing: negative; host-visible phase concluded (later overturned by GP8) | ledger 2026-11-24 08:45 [V*] |
| 2026-11-24 09:20 | ROM dump | mask ROM host-readable; 16 KiB dumped (sha256 fcfd464f...); vectors/IRQ/FIQ targets decoded | ledger 2026-11-24 09:20 [V*] |
| 2026-11-24 10:00 | rom-dump lane | **root cause**: our 0xCECE writes clobber the ROM 0xCEED handshake; release-only protocol defined | ledger 2026-11-24 10:00 [V]/[V*] |
| 2026-11-24 10:40 | **GP8** | **firmware boots exactly like the vendor**: golden page, state 1, mask 0, TCM identical; D2H notify staged | ledger 2026-11-24 10:40 [V*] |
| 2026-11-24 11:10 | GP9 | post-boot service: one D2H notify event drained; IRQ layer still masked; H2D probe not acked | ledger 2026-11-24 11:10 [V*] |
| 2026-11-24 11:40 | GP10 | on-demand CECE + vendor-order service: boot stays golden, no bit2 event, mailbox still dead | ledger 2026-11-24 11:40 [V*] |
| 2026-11-24 12:20 | hcc-service first fire | golden boot; "scheduler fingerprint absent" (later corrected - instrument aliasing) | ledger 2026-11-24 12:20 [V*] |
| 2026-11-24 13:10 | D2H decode | device group-A rings + ACP bodies readable directly; g4 id1 announce decoded | ledger 2026-11-24 13:10 [V*] |
| 2026-11-24 14:00 | d2h-direct fire | **complete ready handshake received** (id1 + id2 payload 0x01060850; 21 frames, 0 overruns) | ledger 2026-11-24 14:00 [V*] |
| 2026-11-24 15:10 | H2D announce | first valid H2D SR frame delivered to the device ring (device consumed the node); firmware mailbox not acked; sysfs grammar requires no-newline write | ledger 2026-11-24 15:10 [V*] |
| 2026-11-24 16:10 | PD fire | image exception/vector path reached for the first time; crude poll patch data-aborts, page clobbered | ledger 2026-11-24 16:10 [V*] |
| 2026-11-24 17:20 | swarm landing | F4 latent transport bug found (region-3 20-bit mask); h2d-frames + hcc-core land; F4 propagated | ledger 2026-11-24 17:20 [V]/[V*] |
| 2026-11-24 18:25 | PD2 analysis | PD data abort root-caused (handshake-retry body before hcc init; nested ABT frames explain the page clobber); clean PD2 design | ledger 2026-11-24 18:25 [V] |
| 2026-11-24 19:00 | PD2-A | PD2 patch breaks the boot: the "dead" config-3 cave is LIVE on our boot; control boot golden | ledger 2026-11-24 19:00 [V*] |
| 2026-11-24 20:00 | driver-clean landing | clean-room instrument driver merges transport+D2H+core; F4 fixed with real region-3 translation; 0 warnings; 133/133 selftest | ledger 2026-11-24 20:00 [V] |
| 2026-11-24 21:10 | clean F4 fire | golden boot + READY_TXQ_SYNC; **g+0xB4/+0xB8 flush retires** (F4 verified on hardware); one announce not yet acked | ledger 2026-11-24 21:10 [V*] |
| 2026-11-24 22:10 | **PD3 fire** | firmware H2D dispatcher alive: one announce **consumed in 0 polls** (h2d_complete done=1), golden page/state/mask intact | ledger 2026-11-24 22:10 [V*] |
| 2026-11-24 22:45 | PD3 record | PD3 cave hardware-validated; out[2]/out[5] reconciled as non-latching pulse observables; pass criterion corrected | ledger 2026-11-24 22:45 [V] |
| 2026-11-24 23:40 | TXQ sync 48 B | first real post-ready message delivered but firmware faults (mask 0x12); 48-vs-72 node length suspected | ledger 2026-11-24 23:40 [V*] |
| 2026-11-25 00:20 | txq72 | fault reproduces; exception record read: pc = WRAM data, evidence in the g0 id1 handler path (0x81039/0x43DF5) | ledger 2026-11-25 00:20 [V*] |
| 2026-11-25 01:10 | control stub | payload-free g0 id0 stub consumed, NO exception: transport proven; fault is handler-specific | ledger 2026-11-25 01:10 [V*] |
| 2026-11-25 02:10 | PD4 confirmation | cave survives (stub present before/after); fault reproduces; exception handler cascades dabt->undef | ledger 2026-11-25 02:10 [V*] |
| 2026-11-25 03:10 | ctx-create RE | key 0x0064 config event identified as the only H2D route to ctx+0x190; frame spec synthesized | ledger 2026-11-25 03:10 [V]/[I] |
| 2026-11-25 04:10 | ordered lap (type 3) | both messages consumed; type 3 takes the no-alloc branch, ctx+0x190 stays 0; TXQ still faults | ledger 2026-11-25 04:10 [V*] |
| 2026-11-25 05:10 | **ordered lap (type 1)** | **context allocated** (ctx+0x190 = 0x01011448), TXQ sync consumed with mask 0 and no staging frame, firmware responds g1 id3 + g1 id0x12 | ledger 2026-11-25 05:10 [V*] |
| 2026-11-25 06:00 | response decode | response frames read byte-exactly from the staged rings: g1 id3 36 B, g1 id0x12 40 B (node lengths match) | ledger 2026-11-25 06:00 [V*] |
| 2026-11-25 07:40 | lap reproducibility | the ordered lap is deterministic on a fresh boot: identical ctx addresses/values and a clean mask-0 outcome | ledger 2026-11-25 07:40 [V*] |
| 2026-11-25 08:40 | data-path + bench-auto fire | frame plane identified as the separate MAC slot-5 ring block; first automated protocol-runner fire exposed and fixed host-side transport/capture bugs | ledger 2026-11-25 08:40 [V]/[V*] |
| 2026-11-25 09:10 | cfg80211 lane lands | wiphy/VAP skeleton (M0-M2) compile-clean host-side; CONFIG_CFG80211 blocker handed to the target lane | ledger 2026-11-25 09:10 [V] |
| 2026-11-25 10:10 | ETE-wake E1a/E1b | the only host-owned DR lever is negative: status/send/fills unmoved; DR copy classified device/ROM-internal | ledger 2026-11-25 10:10 [V*] |
| 2026-11-25 11:00 | proto-seq | g1 id3/id0x12 decoded; the six calibration frames built (cfg ids 3502-3507) | ledger 2026-11-25 11:00 [V] |
| 2026-11-25 11:40 | bench-auto rebuild | protocol-mode capture root-caused to the raw-read transport (resource0 is mmap-only); static bar0read helper, readpath self-check, 312/312 | ledger 2026-11-25 11:40 [V] |
| 2026-11-25 12:30 | **ladder C1** | calibration set_rssi_param (298 B, g0 id2 key 0x0101) consumed in 0 polls; firmware answers 298 B g1 id3; page/state/mask clean | ledger 2026-11-25 12:30 [V*] |
| 2026-11-25 13:30 | wave-2 close | all 7 wave-2 lanes delivered; target build verified (image 22b72852..., kernel slice 13e81214..., cfg80211 config in tree) | ledger 2026-11-25 13:30 [V] |
| 2026-11-25 15:00 | **first Wi-Fi interface** | cfg80211 image flashed to kernelb (full-slot 1794a531...); new kernel booted; wiphy registered after the ready handshake; iw enumerates phy0; VAP wlan0 created and deleted (M0/M1) | ledger 2026-11-25 15:00 [V*] |
| 2026-11-25 16:30 | **ladder C2** | all six calibration messages (3502-3507, 298 B each) consumed in 0 polls; six 298 B g1 id3 answers (catalogue count 7); mask/page clean | ledger 2026-11-25 16:30 [V*] |
| 2026-11-25 18:30 | scan-ap hardware validation | fail-closed module bench-loads; wlan0 netdev up; `iw scan` returns -95/-EOPNOTSUPP by design; VAP-scan lane delivers the byte-exact builders and V1-V4/S1-S2 ladder | ledger 2026-11-25 18:30 [V*]/[V] |
| 2026-11-25 19:30 | wave-3 host deliverables | data-plane host side (dp_*, module 462,952 B sha 8fc693ad), image integration (iw/iwinfo/regdb/kmods), vapscan provider integration (module 676,116 B sha acd069e4) all landed host-side | ledger 2026-11-25 19:30 [V] |
| 2026-11-25 21:30 | **H1/H2 decisive pack** | read-only pack on a PD4 golden boot (2 s and 48 s): pointer field 0x00118D68, live handler table all zero, ROM RAZ 0, anchor 0x00046971 -> PD4 is consume-only; duplex is non-mailbox ETE/polled H2D | ledger 2026-11-25 21:30 [V*] |
| 2026-11-25 23:40 | **first fully automated phase PASS** | C2 PASS: 28 steps, 54/54 checks, 0 failed, 7.02 s, per-phase evidence dir | ledger 2026-11-25 23:40 [V*] |
| 2026-11-26 02:00 | D1 PASS; D2/D2 classification and E1 host-evaluator fixes | D1 21/21 (phy, VAP add/del); D2 reaches the documented -95 and is reclassified BLOCKED-DRIVER; E1 device gates green with one host regex bug left | ledger 2026-11-26 02:00 [V*] |
| 2026-11-26 03:00 | **automated matrix per-phase complete** | C2 PASS 54/54, D1 PASS 21/21, E1 PASS 37/37 (dp_probe/dp_setup/dp_rings + TXQ sync accepted), D2 BLOCKED-DRIVER | ledger 2026-11-26 03:00 [V*] |
| 2026-11-26 04:30 | **first real firmware management message** | g0 id7 user_add 180 B posted and consumed in 0 polls; VAP committed; gaps: g0 id8 refused inflight, scan caps 0x3 vs 0x1d | ledger 2026-11-26 04:30 [V*] |
| 2026-11-26 05:30 | **one-command full matrix** | boot PASS, C2 PASS, D1 PASS, D2 BLOCKED-DRIVER, E1 PASS with bounded between-phase reboots (back in 61.5 s) | ledger 2026-11-26 05:30 [V*] |
| 2026-11-26 06:30 | **three real frames incl. first scan request** | user_add g0 id7 180 B + user_add_notify g0 id8 180 B + scan_req g0 id0xC 520 B, all consumed in 0 polls; no scan results within 5 s -> scan aborted; VAP-start hypothesis | ledger 2026-11-26 06:30 [V*] |
| 2026-11-26 09:00 | T0 deploy attempt | kernel-ABI gate failed closed (running kernel package 5de0e425 vs kmod pin 6f995278) and auto-rolled back cleanly; the five non-kmod packages were then installed manually -> native iw 6.17 + iwinfo from the rootfs | ledger 2026-11-26 09:00 [V*] |
| 2026-11-26 12:00 | **T0 userspace-only deploy PASS** | 49 gates 0 failed; native iw/iwinfo/libiwinfo/regdb persistent; receipt written; VAP-start ladder 2nd fire found the frame-post handler gate (WAL g2 family) | ledger 2026-11-26 12:00 [V*] |
| 2026-11-26 13:00 | **WAL group-2 frames on the wire** | all five mode-0 frames (user_add, notify, WAL vap_add 56 B, WAL vap_start 44 B, scan_req 520 B) consumed in 0 polls; scan still results=0; module-0 VAP object read zero | ledger 2026-11-26 13:00 [V*] |
| 2026-11-26 14:00 | **MILESTONE: first real firmware scan results** | mode-1 VAP base -> vap_add g0 id1 48 B + vap_start g0 id1 40 B + scan_req 520 B consumed; firmware answered 6 x 124 B g1 id17 + 6 x 509 B g1 id15; page/mask clean | ledger 2026-11-26 14:00 [V*] |
| 2026-11-26 15:00 | **D2 TRUE PASS + full matrix complete** | D2 ladder PASS (session result PASS; protocol verdict PASS); matrix boot/C2/D1/D2/E1 all PASS; evidence build/wifi-bench-phase-matrix/20261124T094310Z | ledger 2026-11-26 15:00 [V*] |
| 2026-11-26 16:30 | **first full BSS decode** | 13-channel scan decoded 12 BSS entries from g1 id15 (SSID/DS/HT/RSN/WMM/country); still finish-abort[host-timeout] at 4/13 channels (~3.5 s/ch) | ledger 2026-11-26 16:30 [V*] |
| 2026-11-26 17:00 | **CAPSTONE: one-command all-PASS matrix** | phase_matrix --phase all --reboot-between: boot/C2/D1/D2/E1 = PASS/PASS/PASS/PASS/PASS; evidence build/wifi-bench-phase-matrix/20261124T094716Z | ledger 2026-11-26 17:00 [V*] |
| 2026-11-26 18:00 | ABI-correct image reproduced byte-for-byte (host-only) | container df6a78c3, slice 13e81214, slot 1794a531, rootfs 62344e80; image apk db kernel = 6f995278 = kmod pin; ABI verifier 25/25; flash runbook written (nothing flashed) | ledger 2026-11-26 18:00 [V] |
| 2026-11-26 19:00 | dp-merge host deliverable | merged data-plane provider module 884,576 B ec74fccd + F-AIR ladder (30 steps/83 checks/7 writes; 1,483 host checks); not fired | ledger 2026-11-26 19:00 [V] |
| 2026-11-26 20:30 | first AP fire | hostapd runs after the ucode workaround; fails "Driver does not support authentication/association or connect commands" (CONNECT advertisement gate); a new 466 B g1 id3 D2H frame followed the VAP base; page/mask clean | ledger 2026-11-26 20:30 [V*] |
| 2026-11-26 21:30 | AP v2 fire | wrong build base caused a txq_sync72 timeout (H2D lane closed); hostapd still "Could not configure driver mode"; boot/page clean | ledger 2026-11-26 21:30 [V*] |
| 2026-11-26 23:00 | AP v3 fire + isolation | the txq timeouts were a missing `fw_path=hi_wifi/FIRMWARE.PD4.bin` (unpatched firmware); C2 clean after the fix; manual station<->AP type change works -> the failing step is inside nl80211 setup before the driver op | ledger 2026-11-26 23:00 [V*] |
| 2026-11-26 23:40 | AP v4 fire | .mgmt_tx alone did not pass the AP-setup gate; hostapd-mini -dd is compiled out (no visibility); boot/page clean | ledger 2026-11-26 23:40 [V*] |
| 2026-11-27 00:30 | air-TX fire (fail-closed) | C2 clean; dp_probe was written instead of read -> probe_ok=0; dp_setup/0x0320/tx all refused; no device write; page/mask clean | ledger 2026-11-27 00:30 [V*] |
| 2026-11-27 01:30 | AP v5 fire | mgmt_stypes advertised (AP rx=tx=0x3c15) -> hostapd subscriptions accepted; SET_BEACON hypothesis and the wiphy_new_nm WARN at core.c:532 recorded; boot/page clean | ledger 2026-11-27 01:30 [V*] |
| 2026-11-27 02:30 | **MILESTONE: first data-plane frame on the MAC TX ring** | dp_probe read -> dp_setup (rings/TXQ/14 MAC regs) -> key-0x0320 sync -> dp_tx_frame published wptr 0->1; completion_pending=1 = POST-ONLY; RX 0 captures; page/mask clean | ledger 2026-11-27 02:30 [V*] |
| 2026-11-27 03:30 | AP v6 fire: NEW_BEACON reached start_ap | start_ap reached (beacon_interval=100 dtim=2 head=63 tail=9); ap_ssid 64 B consumed; the next frame's announce unconsumed -> firmware exception (mask 0x18, page lost) = missing mode-1 VAP object (label bug called it ap_auth; it was ap_cfg) | ledger 2026-11-27 03:30 [V*] |
| 2026-11-27 04:30 | **HISTORIC: first live Wi-Fi AP (control plane)** | fresh golden boot; v7 module; hostapd AP-ENABLED, SSID cudy-ap-test; vap_add 48 B -> vap_start 40 B -> ap_ssid 64 B -> ap_cfg 184 B all consumed 0 polls; page/mask clean; ctx0+0x18C changed; VAP ptr 0x01010F18 nonzero | ledger 2026-11-27 04:30 [V*] |
| 2026-11-27 06:30 | **ABI-correct image FLASHED + AP runs on the flashed image** | stock-assisted install: kernel read-back matched slot 1794a531, ubiformat OK, active pair untouched, handover bootflag=b/boot_reg=0x21; boot 6.18.60 from ubi.mtd=rootfsb, selector 0x11; ABI receipt kernel = 6f995278 = kmod pin; native modprobe rc=0 from /lib/modules (no /tmp); T0 full-set verify 38 gates PASS; hostapd AP-ENABLED again, all four AP-start frames consumed 0 polls, page golden/mask 0 | ledger 2026-11-27 06:30 [V*] |
| 2026-11-27 09:00 | AP persistence + new firmware event family | AP still live minutes after start (hostapd processes alive, same SSID/type, no exceptions); D2H catalogue gains g1 id0x23 (35) count=6 24 B + g1 id3 count=20 (466 B) - absent at boot; likely AP/beacon-period activity [I], on-air still unproven | ledger 2026-11-27 09:00 [V*] |
| 2026-11-27 20:30 | tuning baseline (self-contained image + combined module) | heartbeat 1.009-1.011 s, CPU ~idle during AP operation, D2H reader ~145 polls/s (0 overruns), AP persistent; scan does 6 channels in ~0.94 s (~156 ms/ch) then hard-stops | ledger 2026-11-27 20:30 [V*] |
| 2026-11-27 21:30 - 2026-11-28 00:00 | stability boots A-D | stale **module-global** VAP-base latch (A/D) -> `ap_cfg` against a dead firmware VAP object -> exception class; missing real `stop_ap` -> cfg80211 `-EALREADY` on restart (B/C); both root causes hardware-confirmed | ledger 2026-11-27 21:30/23:00/24:00 [V*] |
| 2026-11-28 01:00 | **MAC TX COMPLETED (POST-ONLY solved)** | the vendor **TXQ-table queue-state publish** was the missing device-consumer step; with it `dp_tx_frame` -> `COMPLETED (MAC)` (device rptr == published wptr), page golden/mask 0 | ledger 2026-11-28 01:00 [V*] |
| 2026-11-28 03:00 | scan-tuning: request correct, 6-channel hard stop | wire request is correct (520 B, count 13, dwell 60); firmware engine reads count and iterates, but a hard-coded default 6 in the scan-context stride bounds one request; batching design (6+6+1) delivered | ledger 2026-11-28 03:00 [V]/[V*] |
| 2026-11-28 10:00 | H1 pacing re-arms the scan (7-8 channels) | H1 band-config posted; first channel results past 6 (ch1..ch7); BSS decode proven (`entries=2 iw_visible=2`); page golden, mask 0 | ledger 2026-11-28 10:00 [V*] |
| 2026-11-28 13:30 | **final R4 combined validation + soak** | one boot: AP-ENABLED -> E2 restart AP-ENABLED -> TX COMPLETED; 40-min soak stopped with **242/242 samples golden:1/hostapd=4** | ledger 2026-11-28 13:30/21:00 [V*] |
| 2026-11-28 14:00-19:00 | **static-RE wave, 5 host-only lanes** | rx-static (live bank/headers/consumer patch), wpa-static (key cmds/workers/ops patch), scan-full-static (g0 id10 decode, ceiling closure), sta-static (WAL connect/5 GHz/HT-VHT patch), beacon-static (5-message AP order, no host beacon template); host tests + compile gates + ladders | opensource d343e65/5215bcf/aced90f/13ef742/e80cfc1+d7e9452 [V] |
| 2026-11-28 21:00 | **RX ARM PASSED** | `dp_rx_arm` live bank CA 0x40050000 = status 0x182, mask 0xfffffcc1, norm/small rptr 0x20 (exact vendor mirror); drain moves nothing (no on-air traffic) | ledger 2026-11-28 21:00 [V*] |
| 2026-11-28 22:00 | **five-message vendor AP-start order fired** | `vap_add -> ap_chan (32 B, band=0 ch6) -> ap_ssid -> ap_cfg -> ap_vap_start`; AP-ENABLED, page golden/mask 0 | ledger 2026-11-28 22:00 [V*] |
| 2026-11-28 23:00 | **WPA2-AP GTK install proven** | WPA2 hostapd AP-ENABLED; `key_add` 96 B (idx=1, cipher 0xfac04) + `key_default` 68 B (multi=1) consumed 0 polls; page golden/mask 0 | ledger 2026-11-28 23:00 [V*] |
| 2026-11-28 17:00 | **full 13-channel scan closed (negative result)** | g0 id10 is a BA relay; scan engine uses slot 0 only; no subtype-1 write to +0xFE; ceiling 6 (7-8 with H1); corrections 0x10CD5C/44 B/payload+0x14 | ledger 2026-11-28 17:00; wifi-scan-full [V]/[V*] |
| 2026-11-29 00:00 | static-wave fires summary + integration tasked | 3/3 fired features passed (RX arm, 5-message order, WPA2 GTK) + 40-min soak; all-patch single-module validation and external-observer air test remain; report v5 tasked | ledger 2026-11-29 00:00 [V*] |
| 2026-11-29 01:00 | **FINAL ALL-PATCH MODULE VALIDATED IN ONE BOOT** | one module (stripped 308,080 B sha256 `5b587949...`, 47 params, 0 warnings, opensource `008aca5`) on a fresh golden boot: vendor five-message AP order + hostapd `AP-ENABLED` with WPA2 config; GTK key_add 96 B + set_default 68 B consumed; TX `COMPLETED`; RX arm PASS (live bank vendor mirror); page golden/mask 0/staging 0, no exceptions | ledger 2026-11-29 01:00 [V*] |

End-of-chain bench state (ledger 2026-11-29 01:00): the box runs **our own flashed image** (6.18.60
from rootfsb, selector `0x11`; the ABI-correct container was installed with kernel read-back
matching slot `1794a531...` and the post-boot `apk query kernel` receipt equal to the kmod pin
`6f995278...`), with native `iw` 6.17 / `iwinfo` / `wireless-regdb` in the persistent overlay and
the rootfs-native modules loading from `/lib/modules/6.18.60` (no `/tmp`). The last v4 fire was the
AP on that image: hostapd `AP-ENABLED` with SSID `cudy-ap-test`, all four AP-start messages consumed
in 0 polls, exception mask 0, golden page, and a nonzero firmware VAP pointer; minutes later the AP
was still live and the D2H catalogue had gained the new `g1 id0x23` 24 B family. **The v5 wave then
fired the static features one at a time on fresh golden boots:** MAC TX COMPLETED (TXQ publish), E2
AP restart in one boot, a 40-minute AP soak (242/242 golden), RX arm PASS (live bank exact vendor
mirror), the five-message vendor AP-start order `AP-ENABLED`, and the WPA2-AP GTK install.
Everything ended with page `0x86807FEF` golden, mask 0, staging 0 and no exceptions. **The v5 wave
then culminated in the final all-patch module**: one clean-room module carrying every feature
(stripped 308,080 B sha256 `5b587949...`, 47 parameters, opensource `008aca5`) was validated in a
single fresh boot - vendor five-message AP order + WPA2-AP GTK + TX COMPLETED + RX arm all PASS -
so the individual feature modules are no longer the end state. Stock pair A is untouched and
bootable; the recovery path
is the stock pair / physical recovery. The **host-only** work (static lanes, T0 userspace deploy,
hostapd package set, ABI image reproduction, verifier re-runs) is integrated where cited; none of it
contacted the router, and the **PD4 audit** (no device session; section 6.6,
`wifi-fw-audit/REPORT.md`) still bounds the firmware side.

---

## 8. Corrections register

The ledger is append-only and the newest entry wins. This register records the claims this
investigation superseded, why, and when - because repeating one of them costs bench sessions (two
of them cost recovery cycles). "Where it appeared" names the lane/ledger that carried the older
claim.

| # | superseded claim | corrected claim | decisive evidence | date |
| ---: | --- | --- | --- | --- |
| C1 | bench D2H bitmap `0x4` = "DEVICE READY" | the loaded vendor D2H handler table is {1..7}; bitmap bit2 = an EXCP/vector announce; the real ready handshake is HCC `g4 id1` then `g4 id2` (bounded 2 s / 20 s) | vendor host-replica RE + direct-read bench | 2026-11-23/24 |
| C2 | boot needs release `0x5A5A` **and** a `0xCECE` announce | release-only; `0xCECE` must not be written during boot, only evidence-driven after the handshake is latched | ROM dump + GP8 | 2026-11-24 10:00/10:40 |
| C3 | exception mask `0x0A` = svc + data-abort | `0x0A` = undefined + prefetch; id map is undef=1, svc=2, prefetch=3, abort=4 | rom-dump corrections | 2026-11-24 |
| C4 | `0x7386F44F` is a magic; header `0x1F4/0x1F8` are handler slots | `0x7386F44F` is Thumb code; `0x1F4/0x1F8` are stack tops; image header order is {FIQ, data-abort, prefetch-abort, SVC, undefined} | rom-dump | 2026-11-24 |
| C5 | "S1 is ROM/SoC-internal and not host-triggerable" (GP6 conclusion) | wrong: the gate was being destroyed by **our own** `0xCECE` writes; with release-only the ROM S1 path completes deterministically | GP8 | 2026-11-24 08:45 -> 10:40 |
| C6 | the ETE engine must copy D2H frames to host DRAM before we can receive them | the engine copy is firmware-triggered and idle, but the host can read the device group-A ring nodes and ACP frame bodies directly; that is the working D2H path | d2h-direct bench | 2026-11-24 13:10/14:00 |
| C7 | "the image scheduler never armed; GIC/event-controller reads are 0" | instrument bug: the 20-bit transport macro aliased `CA 0x4016xxxx` reads into region 4; corrected reads show the scheduler armed; the blocker is event 0x4C delivery, not scheduling | fw-postboot regmap audit | 2026-11-24 |
| C8 | `out[3]`/`out[4]` map to BAR0 `0x3B9438/0x3B9414` | they map to `0x4B9438/0x4B9414` (region-3 translation); the wrong writes explain the stuck deferred-notify flush | driver-clean F4 + clean fire | 2026-11-24 20:00/21:10 |
| C9 | PD3's image-header-record cave is safe for a polling stub | the exception-staging routine writes a 17-word frame exactly over `0x4000C..0x4004B`, corrupting the stub; PD4 relocated it to CPU `0x108240` | exception record + PD4 cave check | 2026-11-25 00:20/02:10 |
| C10 | PD3 pass criterion includes `out[5]=1` and `out[2]=8` | those are non-latching write-pulse observables; the pass criterion is `out[0]` `0x8 -> 0` + `h2d_complete/hcc_events done=1` with clean page/state/mask | PD3 reconciliation | 2026-11-24 22:45 |
| C11 | the TXQ config sync (`g0 id1` cmd `0xD8`) is the first real H2D message | the `g0 id1` key-0xD8 handler dereferences `ctx+0x190`, so a context-create (`g0 id1` key `0x64`) must be sent first; the ordered lap is create -> sync | ctx-create RE + bench type-3/type-1 laps | 2026-11-25 03:10-05:10 |
| C12 | context-create type 3 ("minimal") is sufficient | type 3 takes the no-alloc branch; type 1 allocates `ctx+0x190` and makes the TXQ sync clean | ordered lap type 3 vs type 1 | 2026-11-25 04:10 -> 05:10 |
| C13 | PD2's cave CPU `0x47432` (config-3 block) is dead code | it is live on our boot: PD2-A breaks the boot; PD1's epilogue hunk alone was fine | PD2-A bench + control boot | 2026-11-24 19:00 |
| C14 | the g0 id1 fault is a transport/PD3 problem | the transport and the PD3 dispatcher are fine: the payload-free `g0 id0` control stub is consumed with no exception; the fault is inside the g0 id1 **handler** (context precondition) | control-stub fire | 2026-11-25 01:10 |
| C15 | the sysfs arm grammar accepts `echo 1 >` | the grammar parser requires exactly `1` with no trailing newline; use `printf 1 >` | first H2D session | 2026-11-24 15:10 |
| C16 | GP1 proves the reset pulse is sufficient | the pulse is necessary but not sufficient: GP1's occasional successes consumed a stale vendor platform block left in WRAM; the deterministic gate is the `0xCEED` handshake | s1-cpu1 + GP8 | 2026-11-24 06:20/10:40 |
| C17 | no Wi-Fi interface can exist until the whole stack is built | the cfg80211 management interface is independent: wiphy registration gated on the ready pair and the nl80211 VAP netdev lifecycle were hardware-validated on our own image | image flash + bench | 2026-11-25 15:00 |
| C18 | `CONFIG_CFG80211` unset in the target tree blocks any interface | the target config gained `CONFIG_CFG80211=m`; `cfg80211.ko` (2,328,548 B sha256 `949393f9...`) was built, loaded and exercised on the bench image | BUILD-VERIFICATION sec 5 + bench | 2026-11-25 13:30/15:00 |
| C19 | the ETE DR A->B copy may wake from the host-owned published-index write | E1a (write fill) and E1b (write 0) both left status/send/fills unmoved; the DR start condition is device/ROM state, not a missing host write | ete-wake REPORT sec 8 + bench | 2026-11-25 10:10 |
| C20 | g1 id3 (36 B) is the TXQ-sync answer and g1 id0x12 the context-create answer | g1 id3 is a generic config-sync frame dispatched by `syn_id` (`0x0101` alg / `0x0102` cali / `0x07xx` per-VAP); the 36 B frame is an unsolicited VAP-MIB update (syn `0x07E0`), and g1 id0x12 is the ACS-response family forwarded opaquely | proto-seq REPORT sec 2-3 | 2026-11-25 11:00 |
| C21 | Wi-Fi frame data rides the HCC/ETE path, so the idle ETE engine blocks the data plane | frame data uses a separate MAC host-interface ring block (CA `0x40040000/0x40060000`, slot 5); the direct-read constraint is an ETE-message constraint only | datapath REPORT sec 2/8 | 2026-11-25 08:40 |
| C22 | the first TXQ-sync fault was a 48-vs-72-byte node-length problem | the fault was the missing context-create precondition (handler dereferences `ctx+0x190`); the 72-byte node form is the captured wire/node shape, not the fix | txq72 + ctx-create + ordered lap | 2026-11-25 00:20-05:10 |
| C23 | empty benchmark step captures meant a sequence failure | the capture failed because `dd if=resource0` is not a valid MMIO read on the lab kernel (resource0 is mmap-only); a static arm-musl `bar0read` helper fixed it and empty reads now report device-read-empty/MISSING, never PASS | bench-tooling REPORT sec 8.1 + ledger | 2026-11-25 11:40 |
| C24 | an unconsumed announce after a solo TXQ sync proves the firmware consumer died | it can be the exception cascade: the handler fault corrupts the PD3 poller in the staging area; with the context precondition satisfied and the PD4 cave, announces are consumed in 0 polls | txq72/PD4 + ordered lap | 2026-11-25 01:10-05:10 |
| C25 | cfg80211 belongs to the upstream target/kernel series | the cfg80211 config is a BENCH-ONLY addition; it is not part of the luofu kernel patch series or the proposed submission v2 | CFG80211-BENCH-IMAGE sec 5 + upstream REPORT sec 12 | 2026-11-25 13:30 |
| C26 | the wiphy band mapping "domain 0 = 2.4 GHz" is proven | it is an inference (`[I]`) with a `band_force` override; the bench saw both bands listed on the two endpoints, not a per-band link test | cfg80211 REPORT sec 3/10 R4 + bench dmesg | 2026-11-25 15:00 |
| C27 | the firmware event-raise helper writes event 0x4C to CA `0x401611A0` (PD3B design note) | the helper CPU `0xC6DB8` uses base CA `0x40161180` + word index `id>>5`, so 0x4C targets CA `0x40161188` / mirror `0x40161288` (bit 12 = `0x1000`); `0x401611A0` would be id 0x100, outside the helper range. Matters only if the audit's A3 aperture experiment is ever designed | `wifi-fw-audit/REPORT.md` sec 13 C1 + `verify_pd4.py` | 2026-11-24 (audit) |
| C28 | "twin/ack word" table maps `ctx+0x34` to a separate "twin copy B" register | the six context slots `ctx+0x2c..0x40` are `{out[0], out[1], out[2] doorbell, out[3] D2H ack, out[4] D2H rearm, out[5] ack}`; the vendor's "twin" write is simply the out[2] doorbell OR-1 that the clean transport already reproduces - there is no extra host register to write | `wifi-fw-audit/REPORT.md` sec 13 C2 | 2026-11-24 (audit) |
| C29 | PD4 dispatches the pending mailbox bits through the real vendor handler table, so its success proves the handlers execute | **refuted:** the H1 pack shows the sentinel pointer CPU `0x10C1B0` keeps `0x00118D68` and the live table CPU `0x118D68` is all zero, so PD4 is consume-only; the duplex path is non-mailbox ETE/polled H2D, with the dispatcher's ack/clear/doorbell side effects as bookkeeping | H1/H2 read-only pack, ledger 2026-11-25 21:30; F10/6.6 | 2026-11-25 21:30 |
| C30 | C2 (the remaining five calibration messages) was the next open bench sequence | C2 ran 2026-11-25 16:30: all six messages consumed and answered (six 298 B g1 id3); the automated phase later passed 54/54, so the whole calibration boot is proven | ledger 2026-11-25 16:30/23:40; T23 | 2026-11-25 16:30/23:40 |
| C31 | the user/vap-add (`g0 id7/8`, 0xA8) and scan (`g0 id0xC`, 0x1FC) payload structs are open [U], so no firmware VAP/scan operation can be attempted | byte-exact builders now exist and the firmware accepted them: g0 id7 180 B committed the VAP, g0 id8 180 B and g0 id0xC 520 B consumed in 0 polls; the `g0 id9` del builder is exact from the shipped attr. Unpinned fields remain [U], but they did not block acceptance | `wifi-vap-scan/REPORT.md`, ledger 2026-11-26 04:30/06:30; F11/6.7 | 2026-11-26 |
| C32 | the MAC data plane is "mapped only"; the TXQ-table address sync has not been fired | the dp-bringup extension implements `dp_probe`/`dp_setup`/`dp_rings`/`dp_txq_sync`, programs the slot-5 TX/RX rings and sends the vendor's g0 id1 key-`0x0320` sync; automated E1 PASS 37/37 on hardware, no exception. Still no frame has moved | `wifi-datapath-bringup/REPORT.md`, ledger 2026-11-26 03:00; 6.4/6.8/T26 | 2026-11-26 03:00 |
| C33 | a delivered scan request is expected to produce D2H scan results | not yet: the firmware scan handler (CA `0x6494C`) reads VAP id 0 and needs the per-radio VAP object in the started state; `g0 id7/8` only register the mailbox-layer user/VAP, so the scan completes aborted with zero results until the VAP-start family is found | firmware handler decode + scan attempt 2026-11-26 06:30; F11/6.7, Q1 in 10.2 | 2026-11-26 06:30 |
| C34 | the vendor WAL group-2 VAP add/start (cmd `0x0064`/`0x0065`) is the VAP start that unlocks scan results | wrong carrier for our firmware path: the mode-0 WAL frames were consumed but did not populate the firmware VAP object (module-0 context read zero); the **g0 id1** MAC-layer keys `0x0064`/`0x0065` (`vap_start_mode=1`) create/bind it, and with them the firmware produced the first real scan results | ledger 2026-11-26 13:00/14:00; `wifi-vap-start`, `wifi-scan-complete`; F12/6.9 | 2026-11-26 13:00 -> 14:00 |
| C35 | D2 (scan) stays BLOCKED-DRIVER until a new driver feature lands; an all-PASS matrix is not expected | D2 TRUE PASS after the mode-1 provider (results>0, iw rc=0, clean mask/page), and the one-command matrix returns boot/C2/D1/D2/E1 ALL PASS. BLOCKED-DRIVER remains the correct fail-closed verdict whenever results=0 - the classification was not relaxed | ledger 2026-11-26 15:00/17:00; `build/wifi-bench-phase-matrix/20261124T094716Z/`; T28 | 2026-11-26 15:00/17:00 |
| C36 | the v6 AP-start crash was a malformed `ap_auth` frame (`g0 id1` key `0x006D`) | the g0 carrier carries only `ap_ssid` (key `0x0005`, 64 B) and `ap_cfg` (key `0x0127`, 184 B); table A has **no `0x006D` row** and `0x006D` exists only on the WAL envelope. The labelled "ap_auth" frame was `ap_cfg`, and the exception was the missing firmware VAP object (AP workers deref `module_ctx+0x190` with no NULL check) because the AP path never posted the mode-1 VAP base. v7 posts it first and the same frames are consumed cleanly | ledger 2026-11-27 03:30 -> 04:30; `wifi-ap-start/REPORT.md` sec 12; F12/6.11 | 2026-11-27 03:30 -> 04:30 |
| C37 | hostapd fails at SET_BEACON because `change_beacon` is not registered | the initial beacon is `NL80211_CMD_NEW_BEACON`, and cfg80211's SET_BEACON path additionally needs a non-zero AP beacon interval that only a successful start_ap sets - SET_BEACON cannot be the first failure. The real blocker was `NL80211_CMD_REGISTER_BEACONS` being rejected without `WIPHY_FLAG_REPORTS_OBSS`, which made hostapd return -ENOBUFS before building NEW_BEACON | ledger 2026-11-27 01:30; `wifi-ap-start/HOSTAPD-BRINGUP.md` sec 11 | 2026-11-27 01:30 |
| C38 | "Could not configure driver mode" is the driver's SET_INTERFACE op failing | the manual station<->AP type change works with the driver's dmesg; the isolated failure was cfg80211's **management-frame registration** gates - first the missing `.mgmt_tx` op (`nl80211_register_mgmt()` refuses), then the missing `wiphy->mgmt_stypes` table (`cfg80211_mlme_register_mgmt()` refuses). A same-type SET_INTERFACE never reaches the op | ledger 2026-11-26 23:00/23:40, 2026-11-27 01:30; F13/6.10 | 2026-11-26 23:00 -> 2026-11-27 01:30 |
| C39 | a fail-closed `.connect` gate is enough for hostapd's nl80211 init | necessary but not sufficient: it must be paired with a `.disconnect` (the `wiphy_new_nm` WARN at `net/wireless/core.c:532` is `WARN_ON(ops->connect && !ops->disconnect)`) and with `mgmt_stypes`; three distinct gates, each found by a separate fire | ledger 2026-11-27 01:30; HOSTAPD-BRINGUP sec 10/11 | 2026-11-27 01:30 |
| C40 | `dp_probe` is triggered by writing 1 (the E1 doc's attribute spelling) | `dp_probe` is a **READ** action (one read-only MAC word read); a write returns an error, performs no read and leaves probe_ok=0, so dp_setup fails closed. Revision 2 accepts `cat dp_probe` and `echo 1 > dp_probe` as the same read. The 2026-11-27 00:30 fire failed exactly this way | ledger 2026-11-27 00:30; `wifi-dp-air/LADDER-F-AIR.md` attr table | 2026-11-27 00:30 |
| C41 | first air-TX = the MAC has transmitted a frame | the first fire publishes ONE frame to the MAC TX MSDU ring (wptr 0->1 with table read-back) and reads completion_pending=1: **POST-ONLY**. The host->ring->device-table publish chain is proven, but the MAC has not consumed the entry (COMPLETED-MAC = completion_pending 0) and there is no RX capture. A broadcast probe request has no ACK, so on-air proof needs an external observer -> at most air-CANDIDATE | ledger 2026-11-27 02:30; LADDER-F-AIR F4/F5; F14/6.12 | 2026-11-27 02:30 |
| C42 | hostapd `AP-ENABLED` means a working AP | it is a **control-plane** milestone: hostapd -> cfg80211 -> driver `.start_ap` -> provider frames consumed plus a nonzero firmware VAP pointer. Beaconing over the air, client association, WPA2 key install and data-path frame movement are not proven, and the firmware is the PD4-patched research image | ledger 2026-11-27 04:30; 6.11; 10.1 | 2026-11-27 04:30 |
| C43 | the module-0 context lives at BAR0 `0x7D2758` (ledger 2026-11-26 13:00 readback line) | arithmetic slip: the correct BAR0 is `0x7D1758` (CA `0x119758` + `0x6B8000`). The validated cross-check is ctx4 CA `0x11C458` -> BAR0 `0x7D4458`, and the D2 sequence reads ctx4+0x190 at `0x7D45E8` | `wifi-hostapd-deploy/REPORT.md` sec 5 F5 + `tools/check-ladder.py` (107 checks) | 2026-11-26 (readback correction) |
| C45 | the AP milestone ends at "hostapd AP-ENABLED"; the firmware is not demonstrably running AP activity | the 09:00 persistence check strengthened it: the AP stayed live for minutes and the firmware's D2H catalogue gained a new `g1 id0x23` (35) 24 B family (absent at boot, `g1 id3` count 20 x 466 B) - likely AP/beacon-period activity [I]. Still not an on-air beacon proof (needs an external observer); the family's meaning is [U] | ledger 2026-11-27 09:00; REPORT 6.11/F14 | 2026-11-27 09:00 |
| C46 | RX captures 0 means the MAC RX consumer is disabled or misconfigured | wrong: we were polling the config block +0x4c/+0x50/+0x54 (CA 0x40040000), which the vendor never reads; the live pointers are **CA 0x40050000** (+0x00 status, +0x08 norm rptr, +0x0c small rptr, +0x10 completion wptr). The fired `dp_rx_arm` reads status **0x182**, mask **0xfffffcc1**, norm/small rptr **0x20** (exact vendor stock mirror). The completion decoder was also 4 B off: entry = devVA(buffer+4), 36 B header at buffer+4, len at +0x1a, MPDU at buffer+0x28 | [V]/[V*] | `wifi-rx-static/REPORT.md`, ledger 2026-11-28 16:00/21:00; F15/6.13 | 2026-11-28 |
| C47 | the device RX consumer needs an extra enable bit or an H2D RX enable message | no: the vendor's 12-write free/completion-ring program is the **whole device enable**; the missing half was always the **host consumer** (free-ring rptr advance/refill + completion-consumer publish), now implemented behind `rx_enable=1` (`dp_rx_arm`/`dp_rx_drain`) | [V] | `wifi-rx-static/REPORT.md` sec 4/6; F15/6.13 | 2026-11-28 |
| C48 | the vendor AP start is four messages, and posting the VAP start before `ap_cfg` (no channel config) is equivalent | **five** ordered messages: channel config **0x0124 FIRST** -> SSID `0x0005` -> auth `0x006D` (WAL only) -> AP config `0x0127` -> VAP start `0x0065` **AFTER** `ap_cfg`; the host never posts a beacon template (the firmware SMAC beacons autonomously). The fired vendor order reached `AP-ENABLED` with channel 6 programmed before the AP config | [V]/[V*] | `wifi-beacon-static/REPORT.md`, ledger 2026-11-28 22:00; F17/6.15 | 2026-11-28 |
| C49 | full 13-channel scan reachable by programming g0 id10 slots 0..7; R4 dispatch row 0x10CD08, 36 B shape, subtype-2 flag at +0x08 | structurally impossible: scan engine consumes **slot 0 only** and no subtype-1 field writes +0xFE; honest ceiling 6 (7-8 with H1). Corrections: dispatch row **0x10CD5C** (table 0x10CCFC), vendor shape **44 B** (32-byte payload), subtype-2 flag at **payload+0x14**; H1 is pacing (counter/mode), not a channel count | [V]/[V*] | `wifi-scan-full/REPORT.md`, ledger 2026-11-28 17:00; F18/6.16 | 2026-11-28 |
| C50 | WPA2 keys need host MAC-register programming and are blocked on RX | keys are one g0 id1 config event each: `add_key` 0x0078 (0x44 body), `set_default_key` 0x0079 (0x28), `del_key` 0x007A (8 B) -> firmware workers CA 0x0A5825/0x0A5C9D/0x0A4271; no MAC-register sequence. hostapd WPA2-AP installs the **GTK at AP start** (fired: key_add 96 B idx=1 cipher 0xfac04 + key_default 68 B multi=1, both consumed 0 polls) - GTK proven; only the 4-way PTK is RX-gated | [V]/[V*] | `wifi-wpa-static/REPORT.md`, ledger 2026-11-28 23:00; F16/6.14 | 2026-11-28 |
| C51 | STA connect/auth/assoc are direct g0 messages and the host performs auth/assoc; WAL 0x00d0 is KICK_STA | connect is a **WAL cfg-event opcode 0x0123** (108 B body) and disconnect **0x00d0** (16 B `{bssid, reason, [15]=1}`; the inherited KICK_STA row is cfg80211 disconnect). The vendor is a **connect-SME** driver (`.auth`/`.assoc` NULL; the firmware auths/assocs); completion needs a D2H management decode | [V] | `wifi-sta-static/REPORT.md`; F19/6.17 | 2026-11-28 |
| C52 | the 5 GHz second endpoint is a wiphy per PCI endpoint; VHT cap is 0x00400000 on both bands | one wiphy **per band** on chip 0 (single host MAC, two RF chips; stock WIPHY=2): 2.4 GHz **14** channels, 5 GHz **29** channels; HT cap **0x0062**; VHT 2SS MCS0-9 (`0xfffa`) with cap **0x00400000** (2.4 GHz) / **0x00400064** (5 GHz, 160 MHz + SGI) | [V]/[V*] | `wifi-sta-static/REPORT.md`; F19/6.17 | 2026-11-28 |
| C53 | `module_ctx+0x190` is a reliable VAP-validity indicator for a fresh AP start | bench-refuted: it read zero 100 ms after a fresh VAP-base post **and also zero on a successful AP start** - the field is asynchronous. The beacon-static guard now logs it for a fresh post and fails closed only for a stale pre-existing latch | [V*] | ledger 2026-11-28 21:00/22:00; 6.15 | 2026-11-28 |
| C54 | the data plane can only reach POST-ONLY without a started radio/observer (v4 C41) | POST-ONLY is solved at MAC level: the missing device-consumer step is the vendor **TXQ-table queue-state publish** after the ring push; with it the MAC reports `COMPLETED (MAC)` (device rptr == published wptr). On-air evidence still needs an external observer | [V*] | ledger 2026-11-28 01:00; 6.12/T31 | 2026-11-28 |
| C55 | an AP can start only once per boot / restart needs a reboot (cfg80211 `-EALREADY`) | E2 plain restart is achieved after the stability hardening (real `stop_ap` clearing `ap.beacon_interval` + per-ctx VAP-base latch cleared on user_del/probe); the final R4 combined module restarts the AP in one boot. The in-boot AP-after-scan crash class was the stale module-global latch - fixed; a fresh boot remains the safe ladder rule | [V*]/[V] | ledger 2026-11-27 23:00, 2026-11-28 13:30; 1.2/6.11 | 2026-11-28 |
| C56 | the static-wave features are separate module artifacts and the all-patch integration is pending | integrated: the final all-patch module (opensource `008aca5`, stripped **308,080 B sha256 `5b587949...`**, 47 params, 0 warnings, six documented additive conflict resolutions, reproducible drift-refusing build script) was **fired in one boot** - vendor five-message AP order + WPA2-AP GTK + TX COMPLETED + RX arm all PASS, page golden/mask 0. Remaining gaps are the on-air observer, an RX capture with traffic, a fired STA join, the 5 GHz band config and the image bake | [V]/[V*] | `wifi-stability/FINAL-INTEGRATION.md`, ledger 2026-11-29 01:00; F20/6.18 | 2026-11-29 01:00 |
| C44 | the ABI-correct image/flash is host-only work - "runbook ready, nothing flashed", and the bench still runs the modules from `/tmp` | superseded by the 2026-11-27 06:30 fire: the ABI-correct container was **flashed** with the stock-assisted installer (kernel read-back matched slot `1794a531...`, `ubiformat` OK, active pair untouched, handover `bootflag=b`/`boot_reg=0x21`), the box boots 6.18.60 from `ubi.mtd=rootfsb` with selector `0x11`, the post-boot `apk query kernel` receipt equals the kmod pin `6f995278...`, rootfs-native `modprobe` loads from `/lib/modules/6.18.60` (no `/tmp`), the T0 full-set verify passed 38 gates (one cosmetic receipt-missing), and hostapd reached `AP-ENABLED` on the flashed image | ledger 2026-11-27 06:30; `build/abi-image-artifacts/`; `tools/stock-telnet-install.sh`; `staging-fi/handover2.sh` | 2026-11-27 06:30 |

---

## 9. Verification and reproduction

### 9.1 What was re-run while consolidating this report

Consolidation did not invent evidence; it re-ran the owning lanes' committed device-free checks to
make sure the facts this report leans on still reproduce from the tree as committed. v2 re-ran every
wave-2 lane check whose tools are host-only; v3 re-ran the scan/VAP/data-plane/bench-tooling audit
checks. v4 re-ran the report verifier plus the AP/scan/data-plane/bench-tooling host checks;
v5 re-runs the report verifier plus the **static-wave** lanes that own the new findings (rx-static,
wpa-static, scan-full, sta-static, beacon-static) and the v4 lane checks listed below. All checks are
host-only and device-free:

| check | result | what it pins |
| --- | --- | --- |
| `wifi-hcc-core/tools/build.sh --no-kernel` | **PASS**, 528 checks, 0 failures | the post-ready state machine: boot latch, ready pair, TXQ sync, mailbox coalescing, CECE policy, ring arithmetic |
| `wifi-h2d-frames/tools/build.sh` | **BUILD OK**, 233 passed, 0 failed | 12-byte header contract, byte-exact builders (incl. TXQ sync), refusal policy, ready decoder |
| `wifi-d2h-direct/tools/d2h_selftest.py` | **PASS**, 92/92 | direct-read service: node/frame decode, catalogue, consumer-advance rules |
| `wifi-ctx-create/tools/ctx_create_re.py --selftest` | **PASS**, 40/40 | key tables, handler/worker/helper chain, read pack |
| `wifi-ctx-create/tools/ctx_create_frame.py --selftest` | **PASS**, 26/26 | the context-create frame builder |
| `wifi-bench-tooling/tools/selftest.py` | **PASS**, **563/563** (v4 re-run; was 503 in v3, 312 in v2, 177 in v1) | session runner incl. protocol mode, guards, phase matrix, blocked-outcome classification, the bounded reboot path and the F-AIR sequence/attr policy |
| `wifi-datapath/tools/build.sh` + `fw_dp_tables.py --selftest` | **PASS**, 77/77 + 35/35 | two-plane model, TX/RX ring builders, key tables A/B |
| `wifi-proto-seq/tools/selftest.sh` | **PASS**, 66/66 + 38/38 + 55/55 | config-sync decode, calibration frame builders, alg command table |
| `wifi-ete-wake/tools/ete_wake.py selftest` | **PASS**, 81/81 | ETE trigger chain, host/vendor comparison, BAR0 map |
| `wifi-cfg80211` host steps (replicated device-free) | **PASS**, 454/454 cfg80211 logic + 242/242 inherited H2D | wiphy/VAP model, registration gate, 39-op map |
| `wifi-cfg80211/tools/selftest.py --kdir <tree>` | **144/145**; the single failure is the known provider-symvers redundancy after the real tree began exporting the six cfg80211 symbols (documented in `wifi-upstream-target/BUILD-VERIFICATION.md` sec 5) | source/contract checks |
| `wifi-fw-audit/tools/verify_pd4.py` + `pd4_variants.py` | **PASS**, 14/14 selftest + 14/14 variants (v3 re-run); the 31-check blob comparison was re-run in v2 | PD4 identity/delta, cave census, H1/H2 observation and the diagnostic variant builds; the H1 outcome itself is quoted from the bench record (ledger 2026-11-25 21:30) |
| `wifi-vap-scan/tools/build.sh` (host C99 `-Werror -pedantic`) | **PASS**, 137/137 checks | byte-exact g0 id7/id8/id9/id0xC builders, validator, SR node word1s and the V1-V4/S1-S2 ladder data |
| `wifi-scan-ap/tools/selftest.py` | **PASS**, 294/294 source/contract checks (v3 re-run); the 248-check C selftest is the owning lane's record | provider caps 0x1f, one scan_done, fail-closed defaults, D2H hook, exact task-110 builder mapping |
| `wifi-datapath-bringup` tools (host parts) | **PASS**, 69/69 data-plane logic + 308/308 inherited H2D + 34/34 static decode; module compile against 6.18.60 in v3 | TX/RX ring arithmetic, MAC program, MSDU table, TXQ sync builder and the E1 ladder |
| `wifi-ap-start/tools/build.sh` | **PASS**, host strict compile + **128/128** frame selftest + frame emit (4 files) | the four-message AP-start builders, both carriers, wire validator and refusals |
| `wifi-ap-start/tools/ap_start_re.py --selftest` | **PASS**, **63/63** static probes (pinned local vendor artifacts, read in place) | firmware table A rows, worker CAs, 156 B body offsets and the carrier envelopes |
| `wifi-hostapd-deploy/tools/selftest.sh` | **PASS**, **14/14** (host-only, mutation controls; no device contact) | hostapd package pins, deploy script safety, ladder consistency |
| `wifi-vap-start/tools/build.sh` | **PASS**, **61/61** | the mode-1 VAP add/start builders and the WAL group-2 envelopes |
| `wifi-scan-complete/tools/build.sh` | **PASS**, **373/373** host checks + clean 6.18.60 module build (750,512 B sha256 `389ae6fe...`, 0 warnings) | auto scan window, coverage completion rule, g1 id15 IE decode |
| `wifi-dp-air/tools/build.sh` | **PASS**, merge provenance + **1,483** inherited/merged host checks + F-AIR sequence validation + clean 6.18.60 module build (886,864 B sha256 `ca32aec4...`, 0 warnings) | merged data-plane provider, row layout of T31 |
| `wifi-rx-static/tools/rx_selftest.c` + build | **PASS**, host selftest **51/51**; kernel build RC=0, 0 warnings; patch round-trips byte-for-byte | live-bank offsets, completion/entry/header decode, ring arithmetic, refusals (T32) |
| `wifi-wpa-static/tools/run-host-check.sh` + compile gate | **PASS**, key selftest (cipher classes, three bodies/frames, refusal rules); chain identity reproduces the fired final combined tree (`269,688`/`b5dfb883`, CHAIN-ID-OK); patched module 0 warnings | the WPA2 protocol map (T33) |
| `wifi-scan-full/tools/build.sh` | **PASS**: host selftest **90/90**, static-evidence tool PASS (58/58 in the v5 firmware-only re-run; the lane's committed record is 65/65 with the optional vendor-.ko cross-check), clean 6.18.60 build, inherited scan-complete 196/196 | g0 id10 decode, 44 B shape, slot-0-only ceiling (T35) |
| `wifi-sta-static` tools (`sta_re.py`, host selftest, build) | **PASS**, 2.5.24 + 2.4.15 probe evidence, host selftests green, 6.18.60 build 0 warnings (stripped 278,044 B `333c72a5...`) | WAL STA opcodes, band tables, HT/VHT words (T36) |
| `wifi-beacon-static` tools (`build-host.sh`, `beacon_re.py`, `build-module.sh`) | **PASS**, **65/65** host checks + **33/33** static probes on 2.5.24 and 2.4.15; module build 0 warnings (stripped 276,140 B `300cd392...` after the guard fix) | five-message AP order, no host beacon template (T34) |
| `wifi-stability/tools/build-all-patch-module.sh` | **PASS** in the lane's record: chain re-stage + nine-patch apply with six additive conflict resolutions, 0 warnings, 47 params, artifact identity **308,080 B sha256 `5b587949...`** (opensource `008aca5`); the one-boot validation fire is hardware evidence, not a host test | the final integration module (T38/F20) |
| `wifi-re-report/tools/verify_report.py` (this lane) | **PASS** (see 9.4) | this report's cross-references, markers, claim IDs and key constants |

The remaining lane checks (e.g. `rom_s01.py` 70/70, `romdis.py` 86/86, `fw_pd_builder.py`
56/56, `pd2_re.py` 33/33, the target-tree mocked harness 94/94 + 103/103 + 77/77) were re-run by
their owning lanes in the sessions cited in section 7 and read the local vendor blob / target tree
in place; they are quoted here from those lane records and were not re-executed in this
consolidation pass to avoid concurrent vendor-blob or kernel-tree use while other lanes were
working. That is a bookkeeping distinction only - those checks are committed and runnable per the
lane READMEs.

The one re-run that needs a note is the cfg80211 source selftest: its provider-symbol check fails
by design on the current tree because `Module.symvers` now exports the six `wiphy_*/cfg80211_*`
symbols the lane previously declared in a provider file. The owning lane's compile+modpost steps
pass and the upstream-target lane recorded the same reconciliation; this is a lane follow-up (retire
the redundant provider file), not a report defect.

### 9.2 Reproducing the static claims

All static claim tools read the vendor artifacts **in place** and print derived annotations; none
copies vendor bytes. See [REPRODUCE.md](REPRODUCE.md) for the exact commands. The key ones:

* ROM facts: `wifi-rom-dump/tools/romdis.py selftest` (86) and `info/map/flow/stores/consts/gate`
  against the local 16 KiB ROM dump.
* Image facts: `wifi-rom-flow/tools/fwdis.py`, `wifi-fw-postboot/tools/fwdis.py`,
  `wifi-ete-engine/tools/ete_engine.py selftest` (150), `wifi-h2d-catalog/tools/fw_h2d_tables.py --verify`
  (20 byte-level claims), `wifi-ctx-create/tools/ctx_create_re.py --trace`.
* Protocol constants: `wifi-hcc-core` and `wifi-h2d-frames` selftests (9.1) plus
  `wifi-host-replica/SPEC.md`.
* Firmware patches: `wifi-irq-unmask/tools/fw_pd_builder.py` (56) and
  `wifi-bench-tooling/tools/fw_patch_builder.py` (312-suite) rebuild/verify the PD recipes.
* Frame plane and calibration: `wifi-datapath/tools/build.sh` + `fw_dp_tables.py --selftest`
  (77 + 35), `wifi-proto-seq/tools/selftest.sh` (66 + 38 + 55), `wifi-ete-wake/tools/ete_wake.py
  selftest` (81).
* cfg80211 management layer: the lane's host steps (454 + 242), `gen_cfg80211_symvers.py`, and the
  source selftest (144/145, known provider-symvers redundancy).
* VAP/scan frames and the data plane: `wifi-vap-scan/tools/build.sh` (137), `wifi-scan-ap/tools/selftest.py`
  (294), `wifi-datapath-bringup/tools/build.sh` (69 + 308 + 34, plus the 6.18.60 module build with
  0 warnings).
* AP start and the hostapd integration: `wifi-ap-start/tools/build.sh` (128),
  `wifi-ap-start/tools/ap_start_re.py --selftest` (63), `wifi-hostapd-deploy/tools/selftest.sh`
  (14) plus `deploy-hostapd.sh --check` (88) and `check-ladder.py` (107); the hostapd config
  fixtures validate with a hostapd v2.12 build from the tree's own source (OPEN fixture accepted,
  broken negative control refused).
* The merged data-plane provider: `wifi-dp-air/tools/build.sh` (merge provenance + 1,483 checks +
  F-AIR sequence validation + clean 6.18.60 module build) and `wifi-scan-complete/tools/build.sh`
  (373 checks + clean module build).
* Target/bench image: `wifi-upstream-target/BUILD-VERIFICATION.md` has the exact build commands and
  artifact hashes; the flash steps are in `CFG80211-BENCH-IMAGE.md` sec 3 (bench-only, attended).

### 9.3 Reproducing the bench result

The bench chain is documented in the lanes' BENCH-PROCEDURE files, in this order: clean boot with
the reset pulse -> prepare/upload/verify -> custom-block seed -> hcc_arm/hcc_loop -> release-only
-> golden gate -> ready pair (direct reader) -> one context-create type 1 -> one TXQ sync ->
direct-read the responses. Ladder C1/C2 then runs the calibration set (section 6.2); the cfg80211
observation session adds the module load, ready-gated wiphy registration and the nl80211 VAP
create/delete cycle on the cfg80211 bench image (section 6.3); the real-frame session adds the
provider load with `allow_frames=N` and the VAP/scan requests (section 6.7). The v4 path then adds:
the mode-1 VAP base before the scan/report flow (section 6.9, `vap_start_mode=1`), the hostapd
AP-start flow (packages + ucode, `type __ap`, one bounded scan for the VAP base, then
`hostapd -B`: section 6.10/6.11), and the merged data-plane F-AIR ladder (dp_probe READ ->
dp_setup -> key-0x0320 sync -> one TX frame -> read-only RX poll: section 6.12). The delivery step
then flashed the ABI-correct image (kernel+rootfs from one build, attended, recovery armed) and
re-ran the AP flow on the booted image, closing the kmod-ABI mismatch (section 6.11, correction
C44). The one-command phase matrix (section 6.8) puts boot/C2/D1/D2/E1 behind one fail-closed
command with bounded reboots. The generic ladder with pass gates is [TABLES.md](TABLES.md) T19; the v3/v4
ladder/automation tables are T26-T31.

**This report does not authorise a fire.** Benches are attended, recovery-gated operations; the
router's UART is read-only and host->router TX is physically dead. See the project's
`BENCH-FACTS.md`, `RUNBOOK.md` and `tools/fire-preflight.sh` rules.

### 9.4 The report verifier

`tools/verify_report.py` is a device-free selftest for the document itself. It checks:

1. all companion files exist and are non-trivial;
2. every lane directory/path this report cites exists relative to `opensource/docs/incoming/`;
3. only the four evidence markers appear (no malformed or invented marker token);
4. every relative markdown link resolves to a companion file, and all numbered report sections
   0-13 exist with their expected titles;
5. the claim IDs in [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) are unique, gap-free, numbered 1..N, and
   each maps to a real report section with an evidence marker;
6. a curated set of critical constants (addresses, golden words, frame bytes, hashes) appears in
   the tables exactly as in the committed lane sources it cross-checks (e.g. the TXQ-sync wire
   bytes appear in `wifi-h2d-frames`'s committed selftest, the PD3/PD4 image hashes appear in
   `wifi-irq-unmask`'s reports, the v2 image/slice hashes appear in
   `wifi-upstream-target/BUILD-VERIFICATION.md`, the calibration family in `wifi-proto-seq`'s
   report, the MAC ring block in `wifi-datapath`'s report) and the end-state claims also appear in
   the append-only ledger and `STATUS.md`;
7. the v2 tables T21-T25, the v3 tables T26-T28, the v4 tables T29-T31 and the v5 tables T32-T38
   exist (T1..T38 numbered without gaps), and the report carries findings F1-F20 and section-6
   subsections 6.1-6.18;
8. the v3/v4 milestone vocabulary and constants are cross-checked against the owning lanes and the
   ledger: the scan/VAP provider module hash `4a1c3b9a`/caps `0x1f`, the data-plane module hash
   `8fc693ad`/TXQ key `0x0320`, the AP-start frames (48/40/64/184 B) and `fd8e1439`/206,708 B,
   `REPORTS_OBSS`/`mgmt_stypes`, the AP readback `0x01010F18`/`0x70094810` and
   `AP-ENABLED`/`cudy-ap-test`, the merged module `ca32aec4`/886,864 B, the 124 B/509 B scan
   results, the hostapd package identities, the all-PASS matrix evidence (20261124T094716Z) and
   the ABI image hashes `df6a78c3`/`62344e80`/`6f995278`;
9. the v5 static-wave and final-module vocabulary and constants are cross-checked against the five
   static lanes, the stability final-integration record and the ledger: the RX live bank
   `0x40050000`/`0xfffffcc1`/`0x182`/rptr `0x20` and the 36-byte header
   (wifi-rx-static); the WPA2 key commands `0x0078`/`0x0079`/`0x007A`, workers
   `0x0A5825`/`0x0A5C9D`/`0x0A4271`, cipher `0xfac04` and the fired 96 B/68 B frames
   (wifi-wpa-static + ledger 2026-11-28 23:00); the five-message AP order and channel `0x0124`
   (wifi-beacon-static + ledger 2026-11-28 22:00); the scan-full corrections `0x10CD5C`/44 B/
   `+0x14`/slot-0 ceiling (wifi-scan-full + ledger 2026-11-28 17:00); the STA opcodes
   `0x0123`/`0x00d0`, band tables and HT/VHT words `0x0062`/`0x00400064` (wifi-sta-static); the
   TX-COMPLETED/E2/soak facts (ledger 2026-11-28 01:00/13:30); and the final all-patch module
   `5b587949`/308,080 B/47 params (wifi-stability/FINAL-INTEGRATION.md + ledger 2026-11-29 01:00);
9. no forbidden content classes (private keys, secrets paths, oversized hex/base64 blobs that
   would smell of vendor binaries, per-unit MAC addresses) are present.

Run it with the project venv python. It prints one PASS/FAIL line per check group and exits
non-zero on failure.

---

## 10. Limitations and open questions

### 10.1 Limits of the result

1. **The firmware is closed.** Everything here is interoperability research against a binary
   firmware. No upstreamable clean-room Wi-Fi driver follows from it while the blob remains
   closed; the project's own position is that vendor GPL source is the route to a shippable stack.
   The vendor GPL request is unanswered to date.
2. **PD3/PD4 are research instruments.** They patch the vendor firmware image to service its own
   mailbox. They are bench-validated but must never be presented as product changes. The audit
   (section 6.6) confirms no vendor-clean mailbox-delivery alternative has been demonstrated; with
   H1 confirmed, PD4 is a consume-only mailbox servicer, and a shipped driver on this firmware does
   not exist. **Every v4 bench result - including the first live AP - runs on the PD4-patched
   research firmware.**
3. **The result is message-level + management-level + real-request-level + first-AP-control-plane
   level, not link-level.** Two ordered config messages, six calibration messages, the VAP/scan
   management frames, the TXQ-table address sync, the mode-1 VAP base, the four AP-start messages,
   the first data-plane ring entry and the hostapd `AP-ENABLED` handshake were consumed/accepted;
   real scan results were decoded and a cfg80211 wiphy + VAP netdev + real `.start_ap` were
   hardware-validated. **v5 adds device-level proofs:** the MAC **TX completion** (TXQ-table publish
   -> device rptr == wptr), the **RX live-bank consumer** (`dp_rx_arm` exact vendor mirror), the
   **WPA2 GTK install** and the **five-message vendor AP-start order**. None of that proves a
   working link: no beacon has been observed on the air, no client has associated, no 4-way PTK has
   been installed and no RX frame has been captured with traffic.
4. **`AP-ENABLED` is a host/control-plane milestone.** hostapd's state plus the consumed AP-start
   sequence plus the clean readback prove the control path and the firmware VAP object, not
   beaconing, client service or data-path operation (F14/6.11). v5 strengthens it - the five-message
   vendor order programs the module channel context first (6.15), the WPA2 AP installs its GTK
   (6.14), the AP restarts in one boot (E2) and a 40-min soak was 242/242 golden - and the 09:00
   persistence check adds indirect evidence (live AP state minutes later + a new `g1 id0x23` 24 B
   D2H family). None of it is an on-air proof: beaconing needs an external observer, and client
   association still needs the AP-side management TX path plus MAC RX (the `.mgmt_tx` clean-room op
   is fail-closed).
5. **The hostapd integration fixes are advertisements, not implementations.** `.connect`/
   `.disconnect`, `.mgmt_tx`, `mgmt_stypes`, `REPORTS_OBSS` and `change_beacon` exist to satisfy
   cfg80211/hostapd setup gates; management TX/RX, OBSS reporting, beacon control and WPA2
   `add_key` stay M3/fail-closed. The hostapd package set is a bench fixture (deployed through the
   T0 overlay path; it is not baked into the ABI image container).
6. **Scan results are real but bounded - and the bound is structural.** The 14:00 fire got 6
   channel reports + 6 beacons in 5 s; later runs decoded 12 BSS entries and proved the cfg80211/iw
   BSS path (`entries=2 iw_visible=2`). The v5 static wave closed the 13-channel question: the
   scan engine uses the slot-0 context only and the firmware's own stride default is 6, so the
   ceiling is **6 channels/request** (**7-8** with the H1 pacing config, off by default); a 13/13
   single request is structurally impossible and host batching (6+6+1) is the shipped fallback. The
   scan-request flag bytes/channel-index convention and the g1 id15 trailer semantics (signal 0 mBm
   by default) remain [U]/[I].
7. **Some frames are observed but not fully decoded.** The two v1 responses (g1 id3 36 B, g1 id0x12
   40 B) are decoded structurally (C20); the calibration answer bodies were counted but not
   captured (length-only evidence); the scan-request payload still has unpinned flags/channel-index
   fields [U]; the g1 id15 20-byte trailer, `g1 id16` and `g2 id2` status code spaces are
   partially open [U]/[I].
8. **The context-create frame is synthesized**, not captured from the vendor driver; the vendor's
   sender for key `0x0064` is unproven [I]. Type-1/2/3 semantics are inferred from the allocator
   analysis plus one type-3 and one type-1 lap.
9. **The Wi-Fi interface is M0/M1 + mailbox-layer VAP traffic + a real `.start_ap` + the decoded
   key path.** The wiphy is registered, VAP netdevs can be created and deleted, real g0 id7/id8
   frames create the mailbox-layer user/VAP, the mode-1 VAP base creates the per-radio firmware VAP
   object, hostapd reaches the driver's `.start_ap`, and the five-message vendor order (including the
   channel config) is implemented and fired. The WPA2 key ops are implemented and the GTK path is
   proven; management TX/RX, the 4-way PTK and client association remain absent. By default every
   provider path is fail-closed (`allow_frames=0`, `wpa_keys=0`, `rx_enable=0`, `beacon_static=0`);
   real frames need an attended session with an explicit budget.
10. **The ETE DR direction is closed, not solved.** The host lever (E1a/E1b) is measured negative,
    the direct device-ring read remains the supported RX path, and it is a research instrument: it
    depends on this firmware's internal PKTRAM layout, it is polled with bounded batches and
    overrun reporting (no engine flow control, no DMA to host DRAM), and it is not the vendor ABI.
11. **The MAC data plane is now proven in both directions at the device level, but not on the air.**
    The TX side moved past POST-ONLY: the vendor TXQ-table queue-state publish was the missing
    device-consumer step, and with it the MAC consumed our frame (`COMPLETED (MAC)`, device rptr ==
    published wptr). The RX side is armed and its consumer proven (`dp_rx_arm`: live bank CA
    0x40050000, status 0x182, mask 0xfffffcc1, norm/small rptr 0x20 = exact vendor mirror), but no
    RX frame has been captured because no on-air traffic exists in the bench setup. There is still
    no external observer; a broadcast probe request has no ACK, so the strongest air-side label
    remains air-CANDIDATE. The TX rings assume user index 0 and band 1 is untouched.
12. **The firmware IRQ/thread layer is still dormant, and H1 explains why PD4 works.** PD4 services
    the mailbox registers through its own out[0] poll, but its sentinel branch always substitutes
    the ROM zero table: the image's mailbox **handlers never execute** (H1). The working duplex is
    the non-mailbox ETE/polled H2D path. The image dispatcher, event controller and scheduler
    services the vendor stack depends on remain unexercised; later flows (TX status, PM, beacon
    feedback) may need them.
13. **The bench image, phase matrix and hostapd deploy are research instruments, not a release.**
    The image carries a bench-only kernel config (not in the luofu patch series) and native
    userspace (iw/iwinfo/regdb, later hostapd-mini + ucode) was installed into the running overlay
    through the T0 zero-flash path. The ABI-correct image **was flashed** on 2026-11-27 06:30
    (kernel+rootfs from one build, attended): the ABI receipt matches, the rootfs-native modules
    load from `/lib/modules/6.18.60`, and hostapd reaches `AP-ENABLED` on the booted image. It
    remains a research instrument: the driver is research code, the firmware is PD4-patched, the
    flash wiped the overlay (fresh `/overlay`), and the one-command matrix is an operator tool for
    attended, recovery-gated sessions only.
14. **Per-unit data stays out.** No MAC/serial/calibration value is in this report by policy; the
    VAP address quoted by the lane is a locally-administered documentation address, not a unit MAC.
15. **The on-air beacon proof is still external.** v5 programs the channel context before the AP
    config and reproduces the vendor order, and the firmware SMAC owns beacon generation, but no
    beacon has been observed on the air. Our own `g3 id19/20` are *received*-beacon reports, not a
    proof of our AP; a phone/laptop scan or a second monitor radio is the decisive test.
16. **STA / 5 GHz / HT-VHT are static-only and partly [U].** The STA connect body carries four
    host-buffer addresses whose firmware dereference contract is unverified; the first controlled
    post is the experiment. 5 GHz firmware channel ids, the legacy g0 id0xF join reference fields
    and EAPOL depend on paths not yet decoded/implemented; advertising HT40/VHT is capability only.
17. **The all-patch module is validated in one boot; the air side is not.** The final all-patch
    module (308,080 B `5b587949...`, 47 params) carries every feature and passed the one-boot
    validation of AP (five-message order) + WPA2 GTK + TX COMPLETED + rx_arm (6.18/F20). Remaining:
    the external-observer on-air proof (beacon/association/4-way PTK), an RX capture with traffic,
    a fired STA join, the 5 GHz band config, and baking the final module into the self-contained image.

### 10.2 Ranked open questions

1. **Can the live AP be observed beaconing, and can a client associate?** This is the v5 wall: the
   AP control plane is up (`AP-ENABLED`, SSID `cudy-ap-test`, firmware VAP object present, AP
   restarts in one boot and survived a 40-min soak), the host now reproduces the **five-message
   vendor start order** with the module channel programmed first, and a WPA2 AP installs its GTK -
   but no beacon has been observed on the air and no client has associated. An **external observer**
   (second station/monitor on ch 6) is the decisive test; a monitor capture also settles the
   `g1 id0x23` 24 B family meaning. Association additionally needs AP-side management TX (the
   vendor path is `wal_cfg80211_mgmt_tx`, still the WAL/M4 gap) plus MAC RX and the 4-way PTK.
2. **On-air TX and RX capture with traffic.** TX is now COMPLETED at the MAC level (TXQ publish);
   the remaining air-side step is the external observer (no ACK for a broadcast probe request). RX
   is armed and its consumer proven, so the next RX milestone is a **capture with actual traffic**
   (an external client or a frame the MAC receives). The final all-patch module (rx + wpa +
   beacon order + STA on the full chain) **was built and validated in one boot**: AP (5-message
   order) + WPA2 GTK + TX COMPLETED + rx_arm all PASS; the next step is baking it into the image.
3. **What implements AP-side management TX/RX, association and the 4-way PTK?** The vendor performs
   AP-side auth/assoc in host software (`hmac_ap_up_rx_mgmt` -> `hmac_mgmt_encap_auth_rsp`/
   `asoc_rsp_ap` -> `wal_cfg80211_mgmt_tx`); our `.mgmt_tx` is fail-closed and the RX path has no
   traffic yet. This WAL management-frame family plus the RX data path is the usable-AP blocker; the
   GTK is done, the PTK follows a client association.
4. **What remains of the scan?** The ceiling is settled (6 channels/request, 7-8 with H1 pacing;
   13/13 single request structurally impossible) and the BSS/cfg80211/iw path is proven. Remaining
   is quality of service: batching (6+6+1) behaviour under the final module, the g0 id11 per-slot
   channel bindings [U], and the 5 GHz firmware channel-id table.
5. **What remains on the image track?** The ABI-correct image is flashed and the ABI receipt
   matches; the self-contained rootfs rebuild (v7 ap-start driver + hostapd + ucode, kernel slice
   unchanged) is tasked. Remaining: build the all-patch module, bake it into the self-contained
   image, re-provision the overlay after a flash, and fire the one-boot validation (AP + WPA2 + TX
   COMPLETED + rx_arm) before calling the image final.
6. **Can a calibration answer body and the remaining [U] scan fields be captured** (five flag
   bytes, the channel-index convention)? Both need a bounded, immediate post-fire read or a driver
   logging extension, not a new message class.
7. **What unmasks the firmware's IRQ layer on a real vendor boot, and can the dispatcher be brought
   up without a firmware patch?** H1 closes the PD4 branch question: its handlers never run. No
   host-only vendor-clean mailbox path is proven; the only candidate (spare-iATU aperture onto the
   event controller + one-shot raise of event 0x4C) is an untested device-write class (section 6.6).
8. **What remains of the WAL config-command space?** v5 decoded the STA connect/disconnect
   (`0x0123`/`0x00d0`) and the WPA2 key commands (`0x0078`/`0x0079`/`0x007A`); the AP config body
   and the five-message order are decoded and fired. Still open: the AP-side management-frame TX
   family (`wal_cfg80211_mgmt_tx`), change-beacon/stop payloads, and the WAL variant of the AP
   start (mode-0 carrier). The `g0 id0xF/0x10/0x11` STA join reference fields stay [U].
9. **What do the D2H scan/join/PM result frames** (g1 15/16/17, g2 2/3/4/5/6, g3 17) mean
   field-by-field, and what ordering do they require? The g1 id16/`g2 id2` status spaces and the
   g1 id15 trailer are needed to trust firmware completion.
10. **What is the DR A->B copy's device/ROM start condition**, and could a future firmware patch or
    a vendor-clean host path make the D2H path engine-based instead of direct-read?
11. **Does the H1 pacing re-arm generalise to 5 GHz, and what is the 5 GHz channel-id table?** The
    g0 id10 band config is band-agnostic (same handler) and needs 5 GHz channel ids from the
    firmware table [U]; the per-band wiphy model is decoded but the domain->band mapping of our
    endpoints is [I]. A 5 GHz scan/connect rung would settle both.
12. **Is the first STA join safe to post?** The connect body's four host-buffer addresses have an
    unverified firmware dereference contract; the first controlled `sta_join=2` post (risk = [U])
    plus the D2H management/assoc completion decode are the gate to a working STA, followed by
    EAPOL/RX and the WPA keys.

---

## 11. Publication, provenance, and upstream status

**Artifact policy.** This lane commits text and original code only. No vendor binary, object code,
firmware byte, ROM dump, calibration value, extracted DTS or secret is present. Vendor firmware and
ROM identities appear as SHA-256 digests; the blobs stay at their local paths
(`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`,
`/home/shibbityshwab/staging-fi/rom16k.bin`). Wire-frame hex dumps are protocol facts (headers of
messages), not firmware content; the firmware patch recipes contain only the patch hunks (our own
replacement code plus the 4-byte entry words they modify), consistent with the owning lanes'
committed recipes. The bench modules (`.ko`), the hostapd package set (`.apk`) and the imported
image artifacts are build products: only sizes, sha256 values and derived facts are committed; the
files themselves stay local (gitignored).

**The bench image is part of the experiment, not a deliverable.** The cfg80211 kernel is a
bench-only config addition (it is not in the luofu kernel patch series), the Wi-Fi modules are
loaded from `/tmp` rather than packaged in the rootfs, and the image was installed manually from
stock. The board booted it, enumerated a Wi-Fi wiphy + VAP (section 6.3), accepted the real
VAP/scan management frames (section 6.7), returned real scan results (section 6.9), ran hostapd to
`AP-ENABLED` (section 6.11) and published a first data-plane frame to the MAC ring (section 6.12);
that is a hardware milestone for the research line, not a distributable OpenWrt image and not a
claim of install-path readiness. The native userspace (`iw` 6.17, `iwinfo`,
`wireless-regdb`, later hostapd-mini plus its ucode modules) was installed through the T0
zero-flash path into the running pair-B overlay, not baked into the image container. The
ABI-correct image was **flashed on 2026-11-27 06:30** (attended, recovery armed: kernel read-back
matched the slot hash, `ubiformat` OK, active pair untouched, handover `bootflag=b`/`boot_reg=0x21`);
the box boots our 6.18.60 rootfsb image, the post-boot ABI receipt matches the kmod pin, and the
rootfs-native modules load without `/tmp`. That closes the install-path question for the bench
image (overlay wiped and re-provisioned by design); it does not make the image a product - it is a
bench instrument with a research driver and a PD4-patched firmware. The phase-matrix runner
(section 6.8) and the AP/air-TX ladders are likewise attended operator tools: every real run needs
`--yes-really --attended` and the project's recovery door.

**v5 static-wave artifacts.** The five static-RE lanes (rx, wpa, scan-full, sta, beacon) are
host-only: only derived addresses/offsets/ids, original C and synthetic test vectors are
committed; the patched/fired `.ko` artifacts stay local (gitignored) and no vendor code byte,
firmware byte or calibration value is reproduced. The bench fires quoted in section 7 are the
Lead's attended sessions, and the static lanes' modules are identified by size+sha256 only.

**Why it is publishable.** The repository is an intentionally public research notebook: negative
results, dead ends and the ledger are part of the record. This report is written to that standard:
it marks inference as inference, lists the corrections that cost sessions, and does not present
bench instruments as products. The public repos' pre-policy history caveat (an older lab credential
and vendor objects before a policy purge) is documented in `PUBLISH.md` at the project root and
is not repeated here.

**Upstream status.** Blocked for Wi-Fi: an upstream OpenWrt submission of a Wi-Fi stack for this
board requires a licensable firmware/driver path. The vendor modules declare GPL-2.0 and a GPL
source request names the exact symbol families/headers (wifi-driver-re SUMMARY); it is unanswered
to date. The clean-room line's ceiling is the message-level protocol, the M0/M1 management
interface, the real VAP/scan management-frame requests and the host-side MAC data-plane setup
proven here; the firmware remains closed, PD4 must never ship (H1 confirmed it is a consume-only
servicer), and the MAC/MLME/data path remain vendor code. v4 sharpens this rather than changing it:
the **first live AP is a control-plane milestone** (hostapd `AP-ENABLED` + consumed AP-start
frames + a firmware VAP object) that runs on the PD4-patched research firmware; no upstreamable
Wi-Fi driver, firmware-loading path or beaconing proof follows from it. The v5 static wave does not
change the upstream verdict: it decoded the RX/key/STA/scan/beacon interfaces and fired five
predicted results (RX consumer, WPA2 GTK, five-message AP order, TX completion, 40-min soak) - all
still on the PD4-patched research firmware, with AP-side management TX/RX, the on-air beacon proof
and the GPL source as the standing blockers. The target side did advance: the board tree builds end to end, the GP1 reset
pulse and the boot-pair recorder fixes are committed, the bench image was built and booted, and
the 67 target patches were classified for submission (26 keep / 17 fold / 24 drop -> proposed
30-patch series + DTS-0001). The install path has still never run end to end on a device, the
submission form has not been regenerated, and the history purge is pending - so this is still not
an upstream submission (see `wifi-upstream-target/REPORT.md` and `docs/ROADMAP-OPENWRT.md`). The
upstream-final lane (2026-11-27 07:30) finalized that package and recorded the honest state: the
frozen v1 review form is stale (post-GP1 validated line, DTS-0001 still carries the retired
wifi-pcie-hog, L1 = 55 needs 56), the v2 30-patch form is specified but **not generated**, the Wi-Fi
milestone is documented as research-only on PD4-patched firmware, and the GPL request is
unanswered since 2026-10-01 - so the Wi-Fi AP remains out of scope for the upstream submission
(`wifi-upstream-final/`).

**Licensing of this lane's own material.** The report text is part of the notebook; the verifier
script is original code (GPL-2.0-compatible, matching the sibling lanes). Nothing here is copied
from vendor source.

---

## 12. Lane documentation map

The detail sources for every section of this report. Each lane report carries its own evidence
table and reproduction commands; where this report compresses, the lane expands.

| lane directory | what it owns in this report |
| --- | --- |
| `wifi-rom-dump` | section 3 ROM map, S0/S1, the 0xCEED gate, corrections C3/C4 |
| `wifi-rom-s01` | handshake page contract, ETE engine-global negative, early S0/S1 reads |
| `wifi-rom-flow` | image boot flow, stage table, missing-stage analysis |
| `wifi-s1-cpu1` | who writes `+8=2`, the CPU0->CPU1 TCM handoff, GP1 flakiness mechanism |
| `wifi-chip-reset` | the vendor power/reset pulse, GPIO pins/timing, GP1 plan |
| `wifi-ete-frames` | 12-byte header contract, ready-frame builders, SR node format |
| `wifi-ete-engine` | ETE window map, writers census, engine wake negative |
| `wifi-ete-init` | ETE send-index init extraction and patch attempt |
| `wifi-hcc-re` | HCC handshake/message contract, early ready model (corrected in C1) |
| `wifi-hcc-link` | vendor-link/event model, "sends nothing pre-ready" negative |
| `wifi-hcc-service` | host HCC service implementation, CECE policy |
| `wifi-hcc-core` | tested post-ready state machine (528 checks) |
| `wifi-h2d-catalog` | H2D dispatch tables, mailbox layer, payload-probe safety |
| `wifi-h2d-frames` | byte-exact builders, catalogue data, validators (233 checks) |
| `wifi-h2d-inject` | injector instrument, early safe probes |
| `wifi-h2d-send` | TXQ-sync send path, ordered context-create ladder, g0 id1 handler analysis |
| `wifi-d2h-direct` | direct device-ring D2H receive service (92 checks) |
| `wifi-host-replica` | definitive vendor host post-release behaviour, mailbox service order |
| `wifi-driver-re` | host-driver interface contract, cfg80211 ops map, D2H catalog counts |
| `wifi-firmware-re` | firmware load interface, memory contract |
| `wifi-transport-port` | transport contract, BAR0 windows, bench A/B1/B2 |
| `wifi-irq-unmask` | PD/PD2/PD3/PD4 analysis, recipes, builders, verifiers |
| `wifi-fw-postboot` | post-handshake startup, scheduler/IRQ audit, instrument correction C7 |
| `wifi-fw-link-event` | firmware link/event bootstrap, event-pump negative |
| `wifi-event-router` | device event-controller registry map |
| `wifi-boot-gate` | boot-task post-send chain, 0x48820 gate |
| `wifi-boot-kick` | vendor post-release boot-kick candidates |
| `wifi-fw-ready-gate` | group-4 ready gate analysis (cold-boot negative) |
| `wifi-vendor-userspace` | userspace surfaces, startup trace/HCC mapping, cfg80211 scope, M0-M6 |
| `wifi-driver-clean` | clean-room instrument driver (the bench baseline) |
| `wifi-openwrt-pkg` | OpenWrt KernelPackage for the instrument driver |
| `wifi-bench-tooling` | sections 6.8/7/T28/T31: one-command **all-PASS** phase matrix (boot/C2/D1/D2/E1), protocol mode, blocked-outcome classification, bounded reboot, guards, patch builder, the F-AIR sequence and 563 device-free selftests (v4) |
| `wifi-ctx-create` | context-create message RE, frame builder, verification pack |
| `wifi-cfg80211` | section 6.3: cfg80211 M0-M2 lane - wiphy, VAP model, registration gate, 39-op map |
| `wifi-datapath` | section 6.4 / T24: two-plane model, MAC slot-5 rings, TX/RX formats, key tables A/B |
| `wifi-ete-wake` | section 6.5: ETE trigger chain, host-owned field comparison, E1a/E1b ladder |
| `wifi-proto-seq` | sections 5.7/6.2: config-sync decode, calibration family and response contract |
| `wifi-upstream-target` | sections 6.3/11: target tree fixes, cfg80211 bench-image build/flash, patch classification |
| `wifi-image-integration` | sections 6.3/11: the bench image build (iw/iwinfo/regdb/kmod packages), T0/T1 deploy options |
| `wifi-image-deploy` | sections 7/11: the T0 zero-flash deploy tooling, userspace-only mode and the kernel-ABI failure analysis (task-119) |
| `wifi-abi-image` | sections 6.11/7/11/T21: byte-for-byte image reproduction, from-bytes ABI verifier (25/25), host apk transaction simulation and the attended flash runbook (task-128); the flash itself was executed 2026-11-27 06:30 and the ABI receipt matched (ledger, correction C44) |
| `wifi-upstream-final` | section 11: the finalized upstream submission package, the stale-v1/v2-form analysis, the research-only AP wording, the GPL/PD4 status and the publication checks (2026-11-27 07:30) |
| `wifi-fw-audit` | section 6.6 / F10 / T25: PD4 minimality/safety audit, H1/H2 observation, ranked variants and the vendor-clean alternative analysis (task-115); the H1 **consume-only** verdict is integrated in v3 and stands as the v4 limit (F14/10.1) |
| `wifi-vap-scan` | sections 6.7/7 / F11 / T27: byte-exact g0 id7/id8/id9/id0xC builders, firmware-handler decode (scan handler VAP-0 hardcode), V1-V4/S1-S2 ladder (task-110) |
| `wifi-scan-ap` | sections 6.3/6.7 / F11 / T22/T27: cfg80211 scan/AP wiring, frame-provider contract and caps `0x1f`, bounded announce wait/frame gap, fail-closed gate, the 676,180 B provider module (task-113) |
| `wifi-vap-start` | section 6.9 / T27: the VAP start family decode (mode-0 WAL `0x64`/`0x65` vs mode-1 `g0 id1` keys `0x0064`/`0x0065`), why mode-0 had no firmware effect, the provider extension and the VAP-start ladder (task-117) |
| `wifi-scan-d2h` | section 6.9: scan D2H decode - g1 id15 beacon layout (802.11 body + 20-byte trailer), g1 id17 channel records, the firmware VAP-0 handler analysis (task-118) |
| `wifi-scan-complete` | section 6.9 / T27: scan timing and completion semantics (auto window, channel-coverage finish), the full beacon IE decode into cfg80211 BSS entries (task-123) |
| `wifi-datapath-bringup` | sections 6.4/6.8 / F9 / T26: host data-plane implementation (`dp_*` attrs), MAC slot-5 program, TXQ address sync, E1 ladder (task-111) |
| `wifi-dp-txrx` | section 6.12 / T31: the 12-byte device-RAM MSDU table entry semantics, TX publish/completion read-back, the RX 0x28 B descriptor parser (task-122) |
| `wifi-dp-air` | section 6.12 / T31: the merged data-plane provider module (1,483 host checks), the F-AIR ladder and its COMPLETED/POST-ONLY/NO-RX/BLOCKED-DRIVER outcomes (task-126) |
| `wifi-ap-start` | sections 6.10/6.11 / F12-F14 / T29/T30: the AP-start RE (156 B body, both carriers, four messages), the provider integration, and the hostapd capability gates v2-v7 (task-121) |
| `wifi-hostapd-deploy` | section 6.10 / T24/T30: the hostapd-mini package set, the zero-flash deploy path and the AP-start ladder readback pack (task-125) |
| `wifi-bench-session` | sections 6.8/7: the next-bench-session package - gates, session plan, module/sequence pins and the AP preconditions (task-127) |
| `wifi-rx-static` | sections 6.13/6.4 / F15 / T32: the live MAC RX bank CA 0x40050000, completion/36 B-header decode, the rx_enable/dp_rx_arm/dp_rx_drain patch and the RX ladder (task-137) |
| `wifi-wpa-static` | section 6.14 / F16 / T33: the WPA2 key-install protocol (g0 0x78/0x79/0x7A -> workers 0xA5825/0xA5C9D/0xA4271), the cfg80211 add/del/set_default_key ops and the GTK ladder (task-139) |
| `wifi-scan-full` | section 6.16 / F18 / T35: the g0 id10 per-slot decode (BA relay, slot-0-only scan engine, 44 B shape) and the closed 13-channel ceiling (task-138) |
| `wifi-sta-static` | section 6.17 / F19 / T36: STA connect/disconnect WAL opcodes, the per-band 5 GHz model and the HT/VHT words (task-141) |
| `wifi-beacon-static` | section 6.15 / F17 / T34: the five-message vendor AP-start order, the firmware-owned beacon model and the guard-fix fire (task-140) |
| `wifi-scan-tuning` | sections 6.16/10.2: scan timing/auto-window retuning, H1 pacing sweep, the measured 6/7-8-channel ceiling and the batching design (task-133) |
| `wifi-stability` | sections 6.11/6.18/10.1 / F20 / T38: the A-D state-machine matrix (stale VAP-base latch, missing stop_ap), the E2 restart hardening, the final R4 combined module and the **final all-patch integration module** (308,080 B `5b587949...`, one-boot validation) |
| `wifi-dp-tuning` | sections 6.12/10.1: the TXQ-table queue-state publish that closed POST-ONLY (COMPLETED (MAC)) and the dp_tx_complete_poll (task-136) |
| `wifi-image-polish` | section 11: the deployable rootfs payload (loader, init, uci/modprobe.d, hostapd fixtures, PD4 policy) and the mgmt_diag/hcc_trace gating |
| `wifi-perf-baseline` | section 6.16/10: the 105-metric baseline harness and the measured bring-up/D2H/heartbeat/timing numbers |

Historical lanes still in the tree but not load-bearing for this report: `wifi-framework-port`,
`wifi-shim-port`, `wifi-source-hunt`, `wifi-wal-glue`, `wifi-algcfg-slice`,
`wifi-datapath-slice`, `wifi-driver-integration`, `wifi-thermal-re`, `wifi-openwrt-pkg`
history beyond the package, and the phase-numbered docs under `opensource/docs/`.

---

## 13. Conventions and glossary

| term | meaning |
| --- | --- |
| **CA** | device-side address in the endpoint's internal space (device CA) - not a host address |
| **CPU address** | firmware CPU view (region 5); image file offset + `0x40000` |
| **BAR0 offset** | host-side offset into the endpoint's 16 MiB BAR0 |
| **H2D / D2H** | host-to-device / device-to-host |
| **HCC** | HiSilicon Communication Channel: the device/ host message protocol (12-byte header, groups/ids) |
| **ETE** | the endpoint DMA ring engine (SR send rings, DR receive rings, node = 8 B) |
| **out[0..5]** | the six mailbox registers shared between host and device |
| **PDn** | the n-th firmware patch iteration (research instrument) |
| **GPn** | the n-th golden-path bench iteration of the boot protocol |
| **golden state** | page `0x86807FEF / 7 / 2 / 0xABCD`, state 1, mask 0: the vendor-equivalent post-boot state |
| **latch** | record a one-time observation (e.g. page `+8 == 2`) instead of re-gating on a live read |
| **field A/B/C** | the three untyped header bytes at `+0x02/+0x03/+0x08` whose meaning is message-specific |
| **pre-S1 state** | page `0x86803FEF / +8 = 0 / 0xABCD`, state 0: the ROM handshake incomplete |
| **RAM-fallback page** | the pre-S1 fallback signature with ROM boot-log values (`0x1BD000/0x1BD0D8`) |
| **cfg80211 / wiphy** | the Linux wireless management layer; a wiphy is one radio instance advertised to nl80211. Our M0/M1 = wiphy registration + VAP netdev lifecycle. |
| **VAP** | virtual access point: an interface on the wiphy and (on the firmware side) a user/vap object; created here via nl80211 `interface add` |
| **M0 / M1 / M2** | cfg80211 milestones: M0 wiphy+netdev model, M1 VAP lifecycle, M2 scan/station stubs |
| **MAC host-interface block** | device MAC block at CA `0x40040000`/`0x40060000` (slot 5) that carries the Wi-Fi frame DMA rings, separate from HCC/ETE |
| **MSDU-info entry** | 8-byte TX ring entry `{u32 buf_devva; u32 ctl}` with len/path/flag/tid/vap bitfields |
| **TXQ table** | 0x2524-byte host-DRAM table whose device VA is synced by the H2D `g0 id1` key `0x0320` message |
| **calibration ladder C1 / C2** | C1 = one calibration message (`set_rssi_param` 3507) after the ordered lap; C2 = the remaining five in vendor order |
| **E1a / E1b** | the ETE DR published-index experiments (write `+0x38` = fill / = 0) that came back negative |
| **image slice / slot hash** | the `kernel.bin` extracted from the sysupgrade container (3,372,638 B sha256 `13e81214...`); its full-slot hash `1794a531...` is what the flash verifies in kernelb |
| **C2** | the six-message calibration boot in vendor order (create + TXQ + cfg ids 3502-3507), hardware-validated and later phase-automated (54/54) |
| **provider / caps** | the clean driver's registered frame builder (`vapscan-0e4ad45`); capability bits select which cfg80211 op may build a frame (`0x1f` = scan_req+scan_parse+user_add+user_add_notify+user_del) |
| **user_add / user_add_notify** | H2D g0 id7/id8, the VAP-create pair; `user_add` commits the mailbox-layer user/VAP, notify advertises its alg/capabilities |
| **scan_req** | H2D g0 id0xC (`hmac_enc_scan_send_req`), 520 B wire; the firmware's scan handler needs VAP id 0 in the started state to produce results |
| **VAP start wall** | the still-open per-radio VAP object create/start family (WAL group-2 AP start or `g0 0xF/0x10`) that must exist before scan results flow |
| **phase matrix / BLOCKED-DRIVER** | `phase_matrix.py` runs boot/C2/D1/D2/E1 in one command with bounded reboots; BLOCKED-DRIVER is the honest pending-feature verdict (currently D2 scan), never a PASS |
| **dp_* attrs / E1** | the data-plane sysfs attributes (`dp_probe`, `dp_setup`, `dp_rings`, `dp_txq_sync`) and the automated E1 phase that programs the MAC slot-5 block and sends the TXQ address sync |
| **H1 (consume-only)** | the confirmed PD4 branch behaviour: the sentinel pointer keeps its static value and the live handler table is zero, so the firmware mailbox handlers never execute and the duplex rides the non-mailbox path |
| **mode-0 / mode-1 VAP start** | the two VAP-start families: mode 0 = WAL group-2 vendor order (cmd `0x64`/`0x65`; consumed on the bench but it did not create the firmware VAP object), mode 1 = `g0 id1` MAC-layer keys `0x0064`/`0x0065` (the bench-proven carrier; `vap_start_mode=1`) |
| **mode-1 VAP base** | the once-per-load pair vap_add `g0 id1` 48 B + vap_start `g0 id1` 40 B that creates the firmware VAP object at `ctx+0x190`; required before scan results and before the AP frames |
| **AP start sequence** | the four post-ready AP messages: vap_add (48 B) -> vap_start (40 B) -> ap_ssid (`g0 id1` key `0x0005`, 64 B) -> ap_cfg (`g0 id1` key `0x0127`, 184 B), consumed in 0 polls each |
| **AP-ENABLED** | hostapd's state after the driver's `.start_ap` returned success; a host/control-plane fact, not proof of beaconing |
| **hostapd-mini / ucode** | the OpenWrt hostapd variant (v2.12) and the `/usr/share/hostap/*.uc` wifi-scripts modules it loads; bench-deployed through the T0 overlay path |
| **mgmt_stypes** | `wiphy->mgmt_stypes[iftype].rx/tx` bitmaps; cfg80211 accepts hostapd's `NL80211_CMD_REGISTER_FRAME` subscriptions only when the AP bits are advertised (v5 fix; AP rx=tx=`0x3c15`) |
| **REPORTS_OBSS** | `WIPHY_FLAG_REPORTS_OBSS`; userspace-AP-SME hostapd registers `NL80211_CMD_REGISTER_BEACONS` before building NEW_BEACON, and cfg80211 refuses that registration without the flag (v6 fix) |
| **`type __ap`** | the interface type hostapd expects: it always issues SET_INTERFACE(AP) during init, and creating the VAP as AP type makes that a same-type no-op instead of a driver type-change |
| **POST-ONLY / COMPLETED (MAC) / air-CANDIDATE** | air-TX ladder verdicts: POST-ONLY = the host published the ring entry + wptr but the MAC did not consume it (completion_pending=1); COMPLETED (MAC) = completion_pending=0; air-CANDIDATE = an external observer is still required for any on-air claim |
| **F-AIR ladder** | the air-TX/RX ladder of wifi-dp-air: boot/control -> mode-1 VAP + scan precondition -> `dp_probe` (READ) -> `dp_setup` -> key-0x0320 sync -> one TX frame -> read-only RX poll |
| **T0 deploy / ABI-correct image** | T0 = zero-flash persistent apk install into the running pair-B overlay (no flash/selector/reboot; rollback `apk del`); ABI-correct image = the rootfs apk db kernel version equals the kmod `kernel=` pin (`6f995278...`), so a kernel+rootfs flash from one build is the fix (runbook ready) |
| **`finish-ok[channel-coverage]`** | the host-side scan completion rule: success when every requested channel was reported (g1 id17) plus a 500 ms settle window; a firmware completion (g1 id16 status 1 / g2 id2) still wins |
| **readback pack** | the read-only BAR0/CA gate set used for every AP/air-TX PASS: golden page, state, exception mask, staging frame, module-table slot 0, `ctx0+0x18C`, VAP pointer `ctx0+0x190` |
| **live bank (MAC RX)** | the separate host-MAC register block at **CA 0x40050000**: +0x00 status, +0x08 norm free-ring rptr, +0x0c small rptr, +0x10 completion wptr; the config block CA 0x40040000 carries ring addresses/sizes and the host-owned pointers |
| **dp_rx_arm / rx_enable / dp_rx_drain** | the RX lane's read-only live-bank validation action, its fail-closed module parameter (default 0), and the only pointer-moving RX action (bounded completion consume + refill) |
| **completion entry / 36 B header** | each completion entry is the device VA of buffer+4; the device header is 36 B at buffer+4 with payload len at header+0x1a, next sub-MSDU at +0x20 and the MPDU at buffer+0x28 |
| **WPA2 GTK / PTK** | group temporal key installed by hostapd at AP start (proven: key_add 0x0078 + set_default_key 0x0079) vs the pairwise transient key of the 4-way handshake (still RX/association-gated) |
| **key commands 0x0078/0x0079/0x007A** | add_key (0x44 body) / set_default_key (0x28) / del_key (8 B) as g0 id1 config events to firmware workers CA 0x0A5825/0x0A5C9D/0x0A4271 |
| **five-message AP order** | channel config WAL cmd **0x0124 FIRST** -> SSID 0x0005 -> auth 0x006D (WAL only) -> ap_cfg 0x0127 -> VAP start 0x0065 **AFTER** ap_cfg; the host never posts a beacon template |
| **ap_chan** | the 4-byte channel-config body {band, vendor per-VAP byte, channel, unused} sent as g0 key / WAL cmd 0x0124; worker CA 0x841B8 programs module ctx +0x10..+0x13 |
| **SMAC beacons** | the firmware's own beacon engine (TBTT ISRs + suspend/resume handlers) that assembles and transmits beacons autonomously from the VAP context |
| **scan ceiling / H1 pacing** | the measured scan limit (6 channels/request; 7-8 with the H1 pacing config) and the H1 message's real effect (result-counter preset +0x104 and mode +0x103), not a channel count |
| **STA connect-SME** | the vendor driver model where cfg80211 `.connect`/`.disconnect` post WAL cfg-events (0x0123 108 B / 0x00d0 16 B) and the firmware performs auth/assoc; `.auth`/`.assoc` are NULL |
| **per-band wiphy** | 5 GHz is a second wiphy for a logical band on chip 0 (single host MAC, two RF chips), not a wiphy per PCI endpoint; vendor tables 14 (2.4 GHz) / 29 (5 GHz) channels |
| **TXQ publish / COMPLETED (MAC)** | after the ring push the vendor publishes the TXQ-table queue state (per-TID count/SN/bytes + timestamp); with it the MAC consumes the frame and device rptr == published wptr |
| **E2 restart / soak** | an AP stop+restart in one boot after the stability hardening (a real stop_ap clears ap.beacon_interval); the 40-minute AP soak = 242 samples all golden |
| **static-RE wave** | the five host-only 2026-11-28 lanes (rx, wpa, scan-full, sta, beacon) whose derived-only docs + patches + ladders are integrated in sections 6.13-6.17 |
| **all-patch module** | the final integration build: one clean-room module carrying the polish, dp-tuning, scan R4, stability, rx, beacon-order, wpa and sta features (stripped 308,080 B sha256 `5b587949...`, 47 params); validated in one boot (AP five-message order + WPA2 GTK + TX COMPLETED + RX arm) |

Conventions used in the tables: `??` = header byte not initialised by the firmware builder;
"BAR0 = CPU + 0x6B8000" is the CPU->BAR0 rule; "BAR0 = 0x3B8000 + (CA - 0x40000000)" is the IO-CA
rule; `x` in hashes is an abbreviated digest whose full value is in the owning lane report.




