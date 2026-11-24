# BENCH-LADDER - first MAC data-plane TX/RX (ladder F), attended only

Companion to REPORT.md / FRAME-SPECS.md. **Ladder F has NOT been fired.**
Every stage is attended; the standard preconditions are unchanged: recovery
door armed/READY, bench link PASS (tools/bench-link.sh), read-only UART capture
to an absolute path, image/module hashes recorded, no vendor Wi-Fi module
loaded, one clean driver, never rmmod after release.

Image baseline: the current cfg80211 bench image (kernel slice 13e81214, slot
1794a531) with the PD4-patched firmware sha256 cfe24bfb... Module for the
F-POST stages: this lane's patched build of the E1 base lane
(hi5622_wifi.ko 540,936 B sha256 aae8487b4e4dad0c8ec0e20b33ef718e68a4e8a92d78f7f4c11afe1b5c12c502,
vermagic 6.18.60, 0 warnings; built by tools/build.sh against the project
6.18.60 tree). Insert order: cfg80211.ko first (if using the cfg80211 image),
then hi5622_wifi.ko with the bench fw_path/wifi_block_path parameters.

Sequence: `sequences/ladder-f-txrx.json` (21 steps, 205 rendered lines; 12/12
validation checks) is the scripted form of F1-F3. The four new sysfs names need
the allowlist handoff printed by tools/validate_sequence.py:
bench_guards.DP_ATTRS += {dp_tx_frame, dp_tx_status, dp_rx_poll, dp_rx_capture};
DP_WO_ATTRS += {dp_tx_frame, dp_rx_poll}.

## F0. Boot + control plane + E1 (read-only / one setup, one boot)

The E1 ladder is a hard precondition and must have PASSed in this same boot
(DP4 golden page, ready pair, C2 ctx-create, dp_probe, dp_setup rings/table/MAC
program, dp_rings read-back). If any E1 row fails, **stop - ladder F does not
run**. Two extras this lane reads before a frame:

| read | attr | expect |
| --- | --- | --- |
| txrx bound to this load's E1 objects | `cat dp_tx_status` | bound=1 failed=0 posted=0 |
| 0x0320 sync committed | `cat h2d_status` | txq_addr_sent=1 ok=2 timeout=0 |

Stop if dp_tx_status says bound=0 (dp_setup did not bind the txrx state) or if
the 0x320 frame is not committed: the TX action gates on both.

## F1. FIRST data-plane TX entry - ONE frame (same boot as F0)

    echo 1 > /sys/.../dp_tx_frame      # 1 = 32-B broadcast probe request (mgmt)
    cat /sys/.../dp_tx_status

What the driver does exactly once: build the MPDU -> coherent frame buffer
[owner][16-B zero DMA header][MPDU] -> 8-byte MSDU ring entry for user 0/TID 0
slot 0 = {buf_devva=frame+4, ctl(len=32,path=0,hdr=1,tid=0,vap=1)} ->
dma_wmb() -> publish packed wptr 0->1 into the device-RAM table entry
(user0,tid0 CA 0x01060850 / ACP 0x02060850 / BAR0 0x538850) dword1 -> read back
all three dwords. The dev_info line and dp_tx_status carry every number.

Expected (PASS): dp_tx_status `posted=1`, last entry bytes
`{buf_devva=0x8....... ctl=0x00200020}` (len 32, header flag, vap 1),
table image `{ring_devva, 0x0001, rptr}`, no exception (mask CA 0x400008 = 0,
staging 0), page/state still golden.

| observation | meaning | next |
| --- | --- | --- |
| posted=1, table wptr read back 0x0001 | the publish path works | F2 |
| table read-back mismatch | the write did not land | STOP; lane closed for this load (driver sets failed=1); power-cycle, report |
| mask/staging nonzero, or page/state change | firmware exception / handshake disturbed | STOP the boot, power-cycle, capture |
| dp_tx_frame returns -EPERM/-EBUSY | gate refused (no setup/0x320/phase) | no retry; fix the precondition in a later boot |

## F2. Completion observation - read-only

    cat /sys/.../dp_tx_status

completion_pending = distance(device rptr from table dword2, published wptr).

| observation | meaning | next |
| --- | --- | --- |
| completion_pending=0 and table rptr=0x0001 | **COMPLETED**: the MAC engine consumed the entry and wrote the device index | F2b: post frames 2/3 (`echo 2`/`echo 3`, data/QoS-null) and re-check; then F3 |
| completion_pending=1, rptr=0 for the whole session | **POST-ONLY**: host path proven, device did not consume | record; no retry this boot; run F4 (VAP precondition) or close the session |
| pending flips later when dp_tx_status is read | asynchronous consumption | record timing; still COMPLETED |
| new D2H frames appear | the MAC/firmware reacted | capture the catalogue delta (evidence) |

Do not resend a failed frame in the same boot; the driver refuses further posts
after a read-back failure and the budget is 8 per load.

## F2b. Extra frames (optional, same boot)

`echo 2 > dp_tx_frame` (24-B broadcast null-data, vap 2), `echo 3 > dp_tx_frame`
(26-B broadcast QoS-null, vap 2), both ring slots 1 and 2. Expected
posted=2/3. These discriminate the ctl vap-type/header-flag mapping: if the
device consumes a data-class entry but not the mgmt one (or vice versa), the
[I] mapping is settled. One idea per boot: at most the three frames, no retry.

## F3. First RX capture - read-only, one shot

    echo 1 > /sys/.../dp_rx_poll
    cat /sys/.../dp_rx_capture

Bounded 2000 ms poll of MAC +0x4c, then a scan of all 256 norm + 512 small
host-DRAM RX buffers for parseable 0x28-byte descriptors. **No ring pointer is
moved and nothing is reposted.**

Expected: `polled=1 waited_ms=<n> pending=<n> cmp_wptr=... cmp_rptr=... 
norm_rptr=... small_rptr=... captures=<n>` plus per-capture ring/idx/desc/mpdu
hex. A capture with desc_ok=1 and a plausible 802.11 FC byte is the evidence
that the RX completion/descriptor decode can be fixed from real bytes.

| observation | meaning | next |
| --- | --- | --- |
| captures>0, desc_ok=1, plausible FC | RX path evidence captured | decode the completion-word format from the raw word + counters, then enable consume/repost in a follow-up boot |
| pending=0, captures=0 | NO-RX: no frame arrived in the window (expected until a VAP started + radio on a channel) | record; RX stays [U]; no retry |
| cmp_wptr moved but no parseable descriptor | the mapping/host scan needs the real word/format | capture the raw word (already in dp_rx_capture), report |
| mask/staging nonzero or reboot | firmware exception | STOP, power-cycle, capture |

## F4. VAP precondition (merge step; NOT part of this module)

The TX air side needs the firmware VAP/user object the project's working chain
creates through the mode-1 VAP ladder (provider module 738,112 B sha
e09dc697...: vap_add g0 id1 key 0x64 48 B + vap_start g0 id1 key 0x65 40 B
consumed after user_add/notify). That provider is a different build from this
E1+txrx module; merging this lane's two objects into the provider module is an
integration-lane task (Kbuild/source merge on the same base tree). Once merged,
the same dp_tx_frame actions run after the mode-1 VAP create and an actual
on-air transmission can be expected (broadcast frames have no ACK dependency;
add a monitor capture or a second station as the observer).

## F5. Post-run packs (same boot)

| read | location | expect |
| --- | --- | --- |
| mask, staging | CA 0x400008/0x40000c (BAR0 0x6F8008/0x6F800C) | 0, 0 |
| state | CA 0x118B60 (BAR0 0x7D0B60) | 1 |
| page | CA 0x1BF000 (BAR0 0x877000) | 0x86807FEF (+4=7, +8=2, +c=0xABCD) |
| out[0], out[2] | CA 0x40039010/0x400392D4 (BAR0 0x3F1010/0x3F12D4) | 0, 0 |
| table wptr/rptr (optional raw read) | BAR0 0x538854 / 0x538858 | wptr == number of posts (0x0001..0x0003), rptr captured |
| SD/exception | rd_catalog delta | capture only (TX entries have no known D2H answer) |

## Decision matrix (all stages)

| signal | stop? | action |
| --- | --- | --- |
| golden page no longer 0x86807FEF | YES | stop all writes; power-cycle; capture |
| exception mask/staging != 0 | YES | stop; record; power-cycle |
| MSDU table read-back != written wptr | YES | lane closed for the boot; no retry |
| dp_tx_frame refused by gate | YES (this boot) | record gate name; precondition must be fixed in a later boot |
| completion_pending stays 1 | no | POST-ONLY classification; go to F4 merge or close |
| 0 RX captures | no | NO-RX classification; RX stays [U] |
| MAC register bus error / reboot | YES | no retry in the same boot; report exposure |
| new D2H frames after a TX post | no | capture/decode, then decide the next session |

## Stop rules

One boot = one setup + one 0x320 + up to three TX posts + one RX poll; never
retry a failed step; never reload/reset inside the same attended session; never
touch band 1, the mailbox/ETE plane, CA 0x400392f0 or the RC misc window; the
recovery door is armed for every session that writes the MAC.
