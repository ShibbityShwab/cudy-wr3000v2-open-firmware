# SESSION-PLAN - exact attended command package (wifi-bench-session)

**Audience:** the Lead (or an operator with the recovery card armed). **Bench
rule:** recovery door READY, UART read-only capture running, bench link PASS,
human at the box. No human = no fire. The package never types to the UART and
never writes the RC misc window / CA 0x400392F0 / mailbox id 5 / group 1/4.

One attended sitting = three phases, each on a **fresh golden boot** (the
driver reboots before every phase and once at the end):

```
B0 preflight -> S 13-channel scan -> reboot -> A AP start -> reboot -> X first TX/RX -> cleanup reboot
```

Rough timing on the bench (measured class): S ~2.5-3 min (boot ~40 s + scan
<=45 s + teardown), A ~3-4 min (boot + base + hostapd stage), X ~3 min. The
driver's per-phase hard timeout is session-timeout (300 s) + reboot deadline
(300 s) + 180 s; a phase that hangs is killed and the session stops.

## 0. Pre-session (host, read-only; no device writes)

    cd /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
    tools/bench-link.sh                          # must PASS
    tools/recovery-card.sh                       # must be READY (test the physical door)
    tools/uart-capture.sh start --label wifi-bench-session   # read-only capture
    cd opensource/docs/incoming/wifi-bench-session
    # hostapd must already be installed in the persistent overlay (T0 apk-add,
    # itself attended, no flash/no reboot); this package does not reinstall it:
    cd ../wifi-hostapd-deploy && ./deploy-hostapd.sh --apply --attended --yes-really
    cd ../wifi-bench-session
    ./tools/check-artifacts.sh                   # pins (hostapd on-box pins + X module/sequence)
    ./host/run-session.sh                        # FULL DRY-RUN: plans + generated scripts + audits

If the dry-run is green for the phases whose lanes have delivered, the session
is ready. Do not edit sequences/targets at the bench: the JSON is the single
source of truth.

## 1. REAL run (one command)

    ./host/run-session.sh --yes-really --attended
    # subset (e.g. while phase X is still DEPENDENCY-PENDING):
    ./host/run-session.sh --yes-really --attended --allow-partial --phases s,a

What the driver does, in order, with no improvisation:

1. checks every artifact pin (sha256/size; any mismatch refuses the run);
2. resolves the phases it can run (pending lanes are reported, not guessed);
3. **phase S**: invokes bench_session with sequences/session-scan13.json
   (cfg80211 mode, scan-complete module, params below, `--reboot before`);
4. classifies S (session + host gates), then **phase A**:
   - invokes bench_session with sequences/session-ap-base.json (ap-start
     module, `--reboot before`) -> base PASS;
   - deploys hostapd + device/ap-fire.sh + device/ap-readback.sh to
     `/root/hostapd-deploy/` (sha-verified per file) and runs the AP stage;
   - runs the read-only readback pack, fetches both device reports;
5. classifies A, then **phase X** (only if its pins are present): invokes
   bench_session with the dp-merge lane's sequence + merged module;
6. issues the **cleanup reboot** (hard deadline), writes
   `build/wifi-bench-session/<stamp>/SESSION-REPORT.md` + `session.json`.

A STOP in any phase ends the session (no retry in the same boot): the driver
still reboots for cleanup and the report names the failed gate and evidence.

## 2. Phase S - 13-channel scan (device actions, exact)

Module: scan-complete provider (748,888 B `b42ff4ec...` or local alt 748,564
B `02162d4a...`). Insmod params (in the sequence JSON):
`allow_frames=16 vap_start_first=1 vap_start_mode=1 vap_start_module=0
frame_gap_ms=100 h2d_send_budget=8 scan_timeout_ms=60000`.

bench_session performs, in this order (the exact generated script is
[generated/phase-s-device-script.sh](generated/phase-s-device-script.sh)):

    # boot ladder (sysfs attrs on /sys/bus/pci/devices/0000:00:00.0)
    echo 1 > prepare ; echo 1 > upload ; echo 1 > verify ; echo 1 > seed
    echo 1 > hcc_arm ; echo 1 > hcc_loop ; echo 1 > release    # 0x5A5A path only
    # gates: windows=1, fw_loaded=1, dmesg diffs=0, blocks seeded,
    #        hcc armed/loop running, cpu_released=1, golden page latch +8==2,
    #        ready pair g4 id1 <=2 s + g4 id2 <=20 s, phase READY_TXQ_SYNC
    insmod /root/cfg80211.ko                     # before the instrument
    insmod /root/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin \
        wifi_block_path=wifi_block.bin allow_frames=16 vap_start_first=1 \
        vap_start_mode=1 vap_start_module=0 frame_gap_ms=100 \
        h2d_send_budget=8 scan_timeout_ms=60000
    # wait for /sys/class/ieee80211/phy0 (wiphy registration after ready)
    echo 1 > h2d_ctx_create                     # ONE g0 id1 key 0x64 (type 1)
    echo 1 > h2d_txq_sync72                     # ONE g0 id1 0xd8 72-byte node
    iw phy phy0 interface add wlan0 type managed  # provider user_add g0 id7/id8
    ip link set wlan0 up
    iw dev wlan0 scan                           # ONE 13-channel scan request

The provider then posts **vap_add g0 id1 48 B + vap_start g0 id1 40 B +
scan_req g0 id0xC 520 B** (the mode-1 VAP create that makes the firmware scan
handler work), the firmware answers with g1 id17 channel results + g1 id15
beacons, and the scan-complete decoder feeds cfg80211; iw prints the BSS list
when scan_done(aborted=0) arrives.

Expected (PASS) device lines:

    cfg80211: scan channel result slot=.. ch=.. (.. MHz)
    cfg80211: scan DONE (finish-ok[channel-coverage], results=13 bss=13 ...
              chans=13/13 ... - reporting cfg80211_scan_done(aborted=0)
    scan: phase=finished results=13/64 bss=13 complete=0 channel=13
          chans=13/13 ...
    scan finish: last=finish-ok ok=1 abort=0 coverage_ok=1 timeout_ms=45000
    frame post 'vap_add' / 'vap_start' / 'scan_req': ... posted 5/16
    dmesg/iw output: BSS lines with SSID/IE data (host gate)

**Scan-window constraint (known, do not hide it):** the request is 60,000 ms
but the module clamps to `HI5622_SCAN_TIMEOUT_MAX_MS = 45000` ms. At the
measured ~3.5 s/channel the 13th channel needs ~45.5 s, so a **12/13 cut at the
45 s clamp is a plausible outcome**. It is classified **STOP** by the 13/13
gate (evidence captured, exit 10), not a silent pass; the follow-up is a
scan-complete lane change (raise the clamp or split the request), not a
same-boot retry.

## 3. Phase A - AP start (device actions, exact)

Module: wifi-ap-start provider (770,304 B `11511980...`). Insmod params:
`allow_frames=16 vap_start_first=1 vap_start_mode=1 vap_start_module=0
frame_gap_ms=100 h2d_send_budget=8 ap_start_mode=0`.

**Stage A-base** (bench_session, [sequences/session-ap-base.json](sequences/session-ap-base.json)):
boot ladder + cfg80211 + wiphy exactly as phase S, then
`h2d_ctx_create` -> `h2d_txq_sync72`, gated on `ok=2 timeout=0 refused=0`
and the clean golden packs. This sequence creates no VAP and posts no AP frame.

**Stage A-ap** (device/ap-fire.sh, run by the driver after base PASS):

    # pre-health (read-only, bar0read): page 0x86807FEF/7/2/0xABCD, state 1,
    # mask CA+0x40008 = 0, staging = 0, out0/out2 = 0  (STOP on any drift)
    iw phy phy0 interface add wlan0 type __ap     # "__ap", NOT "AP" (iw 6.17)
    ip link set wlan0 up
    iw dev wlan0 scan freq 2412                   # ONE scan = mode-1 VAP base
        # provider posts vap_add g0 id1 48 B + vap_start g0 id1 40 B
        # (only inside hi5622_scan_op_scan when vap_start_first=1); a
        # host-timeout abort of this 1-channel scan is acceptable
    # gate: dmesg "frame post 'vap_add'" and "frame post 'vap_start'"
    # read-only: ctx0 (BAR0 0x7D1758) -> ctx0+0x190 (BAR0 0x7D18E8) nonzero
    hostapd -B -i wlan0 -P /root/hostapd-deploy/hostapd.pid \
            /root/hostapd-deploy/hostapd-open.conf
        # rc=0 means the driver's start_ap returned 0; the provider posts the
        # carrier's sequence:
        #   ap_start_mode=0 (g0 id1 static, default): 2 frames -
        #     frame 1/2 ap_ssid (key 0x0005, 64 B) ->
        #     frame 2/2 ap_cfg (key 0x0127, 184 B = 156-B body: beacon_int
        #     100, DTIM 2, ...).  Static table A has no 0x006D (auth) row and
        #     the g0 carrier omits the VAP start (the mode-1 VAP base did it).
        #   ap_start_mode=1 (WAL g2 fallback): 4 frames - ap_ssid, ap_auth
        #     (0x006D), ap_cfg (0x0127), ap_vap_start (0x0065).
    # gate: dmesg "frame post 'ap_ssid'" + "frame post 'ap_cfg'" (mode 0) or
    #       all four labels (mode 1) + "start_ap sequence posted to provider";
    #       NO "not posted"
    # gate: cfg80211_status refused=0 timeout=0 bad=0
    # post-health packs: page/state golden, mask/staging 0, out0/out2 0
    # readback: vap0 = u32(ctx0+0x190) resolved through the project pointer
    # conventions; vap0+0x10 must read 100 (beacon interval) and vap0+0x14
    # must read 2 (DTIM).  If the [I] deref is unresolved, the stage reports
    # PASS-PARTIAL (frames consumed + clean packs, no AP claim).
    # hostapd is left running; the driver's phase reboot removes it.

Decision: **PASS** = all frame gates + clean packs + readback 100/2;
**PARTIAL** = frames consumed + clean packs but readback unresolved;
**STOP** = a frame not posted, provider refusal/timeout, mask/staging
nonzero, page/state drift, hostapd rc != 0, or a resolved readback mismatch.
The AP stage never re-sends a frame in the same boot.

hostapd deployment contract (canonical): the wifi-hostapd-deploy lane's **T0
zero-flash apk-add** installs hostapd-mini + hostapd-common + the two ucode
modules into the persistent rootfs overlay (no flash, no boot-selector, no
reboot). Run it before the session; the phase-A reboot keeps it. The driver
then verifies the on-box identities over ssh before firing the AP stage:
`/usr/sbin/hostapd` sha256 `13cb3a43...`, `/root/hostapd-deploy/hostapd-open.conf`
sha256 `e4f6edc6...` (both pinned in artifacts.env from the lane's manifest);
a missing/mismatched file refuses the stage with the deploy command in the
error. `HOSTAPD_MODE=static` (push one binary + this package's config) remains
as an emergency fallback; the lane manifest is authoritative otherwise.

## 4. Phase X - first MAC data-plane TX/RX

Module: the wifi-dp-air **merged provider+dp** module (884,576 B sha256
`ec74fccd...`, canonical build /tmp/wifi-dp-air-build; provider base 748,888
`b42ff4ec` + E1 datapath + the dp-txrx TX/RX actions). Sequence: the lane's
committed one-command F-AIR ladder
`../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json` (sha256
`96354365...`), loaded into the same bench_session invocation shape as S/A
(mode cfg80211). Load params (from the sequence JSON): `allow_frames=12
frame_gap_ms=100 h2d_send_budget=4 vap_start_first=1 vap_start_mode=1
vap_start_module=0`.

Device actions, in order (all gates in the sequence JSON are authoritative):

    boot ladder + ready pair + cfg80211 wiphy (as S/A)
    echo 1 > h2d_ctx_create                 # g0 id1 key 0x64 type 1
    echo 1 > h2d_txq_sync72                 # g0 id1 0xd8, 72-byte node
    iw phy phy0 interface add wlan0 type managed ; ip link set wlan0 up
    iw dev wlan0 scan                       # VAP add/start + channel for air TX
    cat dp_probe                            # read-only MAC band-0 reachability
    echo 1 > dp_setup                       # rings/table + MAC program (read-back)
    cat dp_rings                            # ring/table geometry evidence
    echo 1 > h2d_txq_addr_sync              # g0 id1 key 0x0320 table hand-off
    echo 1 > dp_tx_frame                    # 32-B broadcast probe request, slot 0
    cat dp_tx_status                        # MSDU table wptr/rptr read-back
    echo 2 > dp_tx_frame                    # 24-B broadcast null-data
    cat dp_tx_status
    echo 1 > dp_rx_poll ; cat dp_rx_capture # ONE read-only RX window (no pointer move)
    # post packs: mask/staging 0, page/state golden, out0/out2 0

Expected verdicts: **COMPLETED** (posted frames landed AND completion_pending=0:
the MAC consumed the MSDU entries), **POST-ONLY** (posted, completion_pending
>0: host path proven, device did not consume), **NO-RX** (RX capture window
empty; RX stays [U]), **BLOCKED-DRIVER** only for a documented pending outcome.
A read-back mismatch, a firmware exception or a page/state drift is STOP. An
on-air claim still needs an external observer - the package never claims one.

## 5. Post-session

1. The driver's cleanup reboot leaves the box freshly booted on the cfg80211
   image; confirm SSH + `dmesg | tail` and the golden page once.
2. Evidence: `build/wifi-bench-session/<stamp>/` - copy the verdict lines into
   the ledger/EVIDENCE-INDEX; the session report already carries the phase
   table.
3. No retries: a STOP phase is analysed offline; the next attempt is a new
   attended boot with a fix (never a same-boot resend, never a module reload).
4. Stop the read-only UART capture; leave TFTP only if a recovery is armed.