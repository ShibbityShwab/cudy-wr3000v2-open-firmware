# LADDER-F-AIR - first air-TX/RX on the merged provider module (attended only)

**Lane:** wifi-dp-air (shared task task-126). **This ladder has NOT been
fired.** Every stage is attended; the standard preconditions are unchanged:
recovery door armed/READY, bench link PASS (`tools/bench-link.sh`), read-only
UART capture to an absolute path, image/module hashes recorded, no vendor
Wi-Fi module loaded, one clean driver, never `rmmod` after the 0x5A5A release.

Image baseline: the current cfg80211 bench image (kernel slice 13e81214, slot
1794a531) with the PD4-patched firmware sha256 cfe24bfb... Module:
`opensource/docs/incoming/wifi-dp-air` build (hi5622_wifi.ko **884,576 B**
sha256 **ec74fccd...** canonical; compare with the file you deploy).
Insert order: `cfg80211.ko` first, then `hi5622_wifi.ko` with the F-AIR
parameters from BUILD-NOTES.md section 5.

One-command form: bench tooling sequence
`../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json` (sha256
96354365867beaedeb77c001fed31a2afdb0d4d2edb949cd898fab2fcc8342f6)
(30 steps / 83 checks / 7 device writes / 4 shell actions; steps below are
the same stages by name). Manual form: the sections below.

## F0 - boot + control plane (read-only / two H2D frames)

The boot ladder (prepare/upload/verify/seed/hcc_arm/hcc_loop/release) must have
completed; golden page 0x86807FEF (+4=7, +8=2, +0xc=0xABCD), state 1, mask 0.

    echo 1 > $D/h2d_ctx_create     # sequence: ctx-create-t1
    cat  $D/h2d_status             # post-create read pack (ctx body PACK)
    echo 1 > $D/h2d_txq_sync72     # sequence: txq-sync72
    cat  $D/h2d_status             # expect frames_sent=2 ok=2 txq_sent=1 ctx_created=1

| read | expect | stop if |
| --- | --- | --- |
| ctx body PACK (BAR0 0x7D4458=4, 0x7D4468=0x00010001, 0x7D488C=0x0100478C) | allocated | mismatch -> STOP (no VAP/scan possible) |
| mask CA 0x400008 / staging 0x40000c | 0 / 0 | nonzero -> STOP |

## F1 - firmware VAP + radio channel: the air-TX precondition

The working chain creates the firmware VAP through the provider's mode-1 VAP
ladder, which is posted **from the scan path** (vap_start_first=1):

    iw phy phy0 interface add wlan0 type managed   # user_add g0 id7 + notify g0 id8
    ip link set wlan0 up
    iw dev wlan0 scan                               # vap_add 0x64 + vap_start 0x65 + scan_req 0xC
    cat $D/cfg80211_status                          # scan: phase=finished results=<n>/... ; scan finish: ...
    dmesg | grep 'scan channel result'

| observation | meaning | next |
| --- | --- | --- |
| iw rc=0, scan phase=finished, results>=1, dmesg channel-result lines | firmware VAP exists + radio had a channel: the air-TX precondition is satisfied | F2 |
| iw rc=0 but results=0 | **BLOCKED-DRIVER**: the firmware produced nothing (known VAP/scan-state wall); there is no firmware VAP to transmit from | a manual operator may stop here; the scripted F-AIR sequence records BLOCKED and - by bench-tooling design (blocked steps do not stop the boot) - still runs F2-F5, so the merged module's host path is exercised and expected **POST-ONLY**; the row verdict stays **BLOCKED-DRIVER**, never an air-TX claim |
| iw nonzero rc / no scan layer | provider or cfg80211 defect | STOP, capture, no retry |

The scan may end in `finish-ok` or `finish-abort` (firmware pace ~3.5 s/channel
against the auto window) - either is fine for this ladder as long as results
were produced. Do not extend/retry the scan in this boot: one scan request.

## F2 - E1 data plane (one setup; hardware-validated E1 actions)

    cat  $D/dp_probe        # read-only one-word MAC reachability read (sequence: dp-probe)
    echo 1 > $D/dp_setup    # sequence: dp-setup (ONE MAC/ring action)
    cat  $D/dp_status       # expect setup_done=1 failed=0 mac_regs=14 table_entries=8
    cat  $D/dp_rings        # expect +0x38/+0x3c=0x8000, +0x48=0xfffffcc1, 8 table entries

A read-back mismatch closes the data-plane lane for this module load (the
driver refuses every later setup/TX request - no retry).

## F3 - TXQ-table address sync (the data-plane hand-off)

    echo 1 > $D/h2d_txq_addr_sync   # sequence: dp-txq-sync (g0 id1 key 0x0320)
    cat  $D/h2d_status              # expect txq_addr_sent=1 ok=3 timeout=0 frames_sent=3

The TX action gates on this commit (this load's context-create + the 0x0320
sync). On failure: STOP, no retry in this session.

## F4 - FIRST data-plane TX frame

    echo 1 > $D/dp_tx_frame      # 32-B broadcast probe request (mgmt, no FCS)
    cat  $D/dp_tx_status         # posted=1, last entry, table image, completion_pending

What the driver does exactly once: builds the MPDU into the coherent frame
buffer `[owner][16-B zero DMA header][MPDU]`; writes the 8-byte MSDU-info ring
entry for user 0/TID 0 slot 0 `{buf_devva, ctl(len=32, path=0, hdr=1, tid=0,
vap=1)}`; `dma_wmb()`; publishes the packed next producer index into the
device-RAM 12-byte table entry dword1 and reads all three dwords back.

| observation | meaning | next |
| --- | --- | --- |
| posted=1, entry + table wptr read back, **completion_pending=0** | **COMPLETED (MAC)**: the MAC engine consumed the entry and wrote table dword2 | F4b (optional data-class frame) then F5 |
| posted=1, **completion_pending=1** for the whole session | **POST-ONLY**: host->ring->table->read-back path proven, the MAC did not consume the entry (VAP/channel/MAC-state wall) | record; do NOT retry this boot; close with POST-ONLY |
| table read-back mismatch | the publish did not land | STOP; lane closed for this load; power-cycle, report |
| mask/staging nonzero or page/state drift | firmware exception / handshake disturbed | STOP the boot, power-cycle, capture |
| dp_tx_frame returns -EPERM/-EBUSY | gate refused (no setup/0x0320/phase) | no retry; fix the precondition in a later boot |

Important honesty limit: a broadcast probe request has **no ACK**, and the
clean-room host can only prove the MAC-completion chain. A *true* on-air
observation needs an **external observer** (a second station/monitor on the
same channel recording the frame). Without it, the strongest verdict this
ladder can produce is COMPLETED (MAC); the report must say "air-CANDIDATE",
not "air-verified". Document the observer setup in the session evidence.

Optional same-boot discrimination: `echo 2 > $D/dp_tx_frame` posts a 24-B
broadcast null-data (data class, vap type 2) at ring slot 1; the pair of
completion results settles the [I] ctl vap-type/header-flag mapping. At most
two posts; never resend a frame that failed.

## F5 - RX capture (read-only, one shot)

    echo 1 > $D/dp_rx_poll
    cat  $D/dp_rx_capture        # polled=1 waited_ms=... pending=... captures=...

Bounded 2000 ms poll of MAC +0x4c then a scan of the 256 norm + 512 small
host-DRAM RX buffers for parseable 0x28-byte device descriptors. **No ring
pointer is moved and nothing is reposted** (the completion-word format is still
[U]). A capture with desc_ok=1 and a plausible 802.11 FC byte is the evidence
needed to decode the format in a follow-up lane; 0 captures is **NO-RX**
(recorded, not a failure).

## F6 - post-run packs + evidence (same boot)

| read | location | expect |
| --- | --- | --- |
| mask, staging | CA 0x400008 / 0x40000c (BAR0 0x6F8008/0x6F800C) | 0, 0 |
| state | CA 0x118B60 (BAR0 0x7D0B60) | 1 |
| page | CA 0x1BF000 (BAR0 0x877000) | 0x86807FEF / 7 / 2 / 0xABCD |
| out[0], out[2] | BAR0 0x3F1010 / 0x3F12D4 | 0, 0 |
| h2d_status | /sys/.../h2d_status | timeout=0 refused=0 txq_addr_sent=1 |
| rd_catalog delta | /sys/.../rd_catalog | capture only (TX entries have no known D2H answer) |
| UART capture | absolute path | full console log for the session |

## Decision matrix (all stages)

| signal | stop? | classification / action |
| --- | --- | --- |
| golden page no longer 0x86807FEF | YES | FAIL/STOP: power-cycle, capture |
| exception mask/staging != 0 | YES | FAIL/STOP: record, power-cycle |
| MSDU table read-back != published wptr | YES | lane closed for the boot; no retry |
| dp_tx_frame refused by gate | YES (this boot) | record gate name; precondition fix in a later boot |
| scan results=0 (F1) | no (scripted; manual may) | **BLOCKED-DRIVER** (no firmware VAP; the scripted ladder still exercises the host path as POST-ONLY evidence, but the row can never claim a TX/air verdict) |
| posted but completion_pending stays 1 | no | **POST-ONLY** classification; close or rerun in a later session |
| completion_pending -> 0 | no | **COMPLETED (MAC)**; air needs the external observer -> **air-CANDIDATE** |
| 0 RX captures | no | **NO-RX**; RX stays [U] |
| MAC bus error / reboot | YES | no retry in the same boot; report exposure |

## Stop rules

One boot = one ctx-create + one txq_sync72 + one scan + one dp_setup +
one 0x0320 + at most two TX posts + one RX poll; never retry a failed step;
never reload/reset inside the same attended session; never touch band 1, the
mailbox/ETE plane, CA 0x400392F0 or the RC misc window; the recovery door is
armed for every session that writes the MAC. A FAIL/POST-ONLY/NO-RX outcome is
a recorded result, not a reason to repeat the action in the same session.

## Sequence mapping (bringup-f-air-txrx.json)

| stage | sequence steps |
| --- | --- |
| F0 | `cfg-status`, `pre-pack`, `ctx-create-t1`, `post-create`, `txq-sync72`, `post-txq` |
| F1 | `vap-add`, `link-up`, `vap-added`, `catalog-baseline`, `scan`, `scan-settle`, `scan-results`, `scan-dmesg` |
| F2 | `dp-probe`, `dp-setup`, `dp-rings-check`, `pre-tx-status` |
| F3 | `dp-txq-sync` |
| F4 | `tx-frame-probe-req`, `tx-status-1`, `tx-frame-null`, `tx-status-2` |
| F5 | `rx-poll`, `rx-capture` |
| F6 | `post-pack`, `h2d-summary`, `catalog-after`, `note-end` |
