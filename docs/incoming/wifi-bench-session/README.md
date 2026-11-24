# wifi-bench-session - attended package: 13-channel scan + AP start + first TX/RX

**Lane:** wifi-bench-session (shared task task-127), **HOST-ONLY package** - the
lane never contacts the router; the Lead fires the session with a human at the
bench. **Date:** 2026-11-26. **Status:** the phase-S and phase-A plans are
dry-run verified end to end (artifact pins, bench_session plans, generated
device scripts, audits); phase X is wired to the wifi-dp-air merged module and
its committed F-AIR ladder (task-126 commit `a615a8c`) and phase A's hostapd
path is wired to the wifi-hostapd-deploy T0 apk-add (task-125 commit `a32f028`;
pins come from that lane's verified on-box manifest).
A real run additionally requires (a) that attended hostapd T0 deploy to have
run first and (b) the wifi-dp-air bench_guards payload-policy fix for the
dp_tx_frame/dp_rx_poll stores (the package's X dry-run stays RED until it
lands; S/A are already green).

This is the exact command package for the next bench session: **one attended
sitting** that runs (1) the full **13-channel scan** with cfg80211 BSS entries,
(2) the **AP start** (hostapd NEW_BEACON -> 4 AP frames -> firmware VAP
readback), and (3) the **first MAC data-plane TX/RX** on the merged provider
module. Each phase gets a fresh golden boot (module reload requires it) and
every phase is fail-closed with a PASS/STOP decision table.

## 1. Why these three phases

Bench state after the 2026-11-26 capstone (all hardware-validated):

- one-command matrix boot/C2/D1/D2/E1 = PASS (golden PD4 page
  0x86807FEF/7/2/0xABCD, state 1, mask 0; six calibration answers; cfg80211
  wiphy + VAP; real scan results; dp setup + TXQ addr sync);
- the last 13-channel fire parsed **12 BSS entries but only covered 4/13
  channels**: the firmware's own pace is ~3.5 s/channel, so the scan-complete
  auto window (3000+900*n = 14.7 s) still expired - this package requests the
  **manual 60 s bound** (`scan_timeout_ms=60000`);
- AP start is fully RE'd (4-message sequence, provider module 770,304 B sha
  11511980) but **never fired** - hostapd is not in the image yet; the
  wifi-hostapd-deploy lane supplies it;
- the data-plane TX/RX objects exist (dp_tx_frame/dp_tx_status/dp_rx_poll/
  dp_rx_capture, module 540,936 B sha aae8487b) but are **not in the provider
  module**; the wifi-dp-merge lane merges them and owns the F-AIR ladder.

## 2. Layout

| path | purpose |
| --- | --- |
| [artifacts.env](artifacts.env) | the single place artifact paths/sha256 live (pending = dependency lane) |
| [host/run-session.sh](host/run-session.sh) | host-side entry point (dry-run by default) |
| [host/session_driver.py](host/session_driver.py) | orchestration: pin checks, per-phase bench_session invocations, hardened reboot-before/cleanup, AP device stage, evidence + report |
| [sequences/session-scan13.json](sequences/session-scan13.json) | **phase S** bench_session sequence (13-channel scan, 13/13 + BSS gates) |
| [sequences/session-ap-base.json](sequences/session-ap-base.json) | **phase A** base sequence (boot -> ctx_create -> txq_sync72); the AP stage is device-side |
| [device/ap-fire.sh](device/ap-fire.sh) | **phase A** device-side AP-start stage: iface type __ap, mode-1 VAP base, hostapd NEW_BEACON, 4 frame gates, firmware readback |
| [device/ap-readback.sh](device/ap-readback.sh) | phase A read-only firmware readback pack (run even after a STOP) |
| [device/hostapd-open.conf](device/hostapd-open.conf) | phase A hostapd config (2.4 GHz ch6, OPEN, beacon 100, DTIM 2) |
| [generated/](generated/README.md) | reviewed snapshots of the **exact generated device-side shell scripts** for S/A/X (rendered by bench_session --dry-run, `sh -n` clean; every run re-renders them into its evidence dir) |
| [tools/check-artifacts.sh](tools/check-artifacts.sh) | host-only pin check |
| [tools/selftest.sh](tools/selftest.sh) | host-only selftest of the whole package |
| [SESSION-PLAN.md](SESSION-PLAN.md) | the exact attended procedure (step order, commands, expected lines, stop rules) |
| [GATES.md](GATES.md) | PASS gates + decision tables for all three phases |

## 3. One-command usage

    cd opensource/docs/incoming/wifi-bench-session

    # 1) DRY-RUN (default): artifact pins, bench plans, rendered device scripts,
    #    audits.  No device contact; safe to run any time.
    ./host/run-session.sh

    # 2) optional: pin checker only
    ./tools/check-artifacts.sh            # rc=0 with pending notes
    ./tools/check-artifacts.sh --strict   # rc=1 while a lane pin is pending

    # 3) REAL attended run (Lead only; recovery door armed by a human):
    ./host/run-session.sh --yes-really --attended
    #    with still-pending lanes, either wait for the pins or:
    ./host/run-session.sh --yes-really --attended --allow-partial --phases s,a

    # sub-phases / hints
    ./host/run-session.sh --phases s          # plan or run one phase
    ./host/run-session.sh --json              # machine-readable session record

Exit codes: **0** all planned/run phases PASS; **12** PARTIAL (a phase is
pending, or phase A passed frames with the [I] readback unresolved); **10**
STOP/FAIL; **2** usage or a refused preflight/pin.

The driver invocation it performs for a phase is exactly:

    python<venv> opensource/docs/incoming/wifi-bench-tooling/tools/bench_session.py \
      --mode cfg80211 --sequence sequences/<phase>.json \
      --report-dir build/wifi-bench-session/<stamp>/<phase> \
      --endpoint 0000:00:00.0 --session-timeout 300 --ready-id1 2.0 --ready-id2 20.0 \
      --module <module> --fw-file <PD4> --fw-manifest <manifest> \
      --wifi-block-bin <2472 B block> --ssh-host 192.168.1.1 \
      --cfg80211-ko <cfg80211.ko> --iw-bin <iw> --libnl-tiny <libnl-tiny.so.1> \
      --module-param <each load param> --reboot before --yes-really --attended

followed, for phase A only, by the device-side stage described in
[SESSION-PLAN.md](SESSION-PLAN.md) section 4.

## 4. Phase summary (what fires, with which module and params)

| phase | mode | module (sha256) | insmod params | verdict gates |
| --- | --- | --- | --- | --- |
| **S** 13-channel scan | cfg80211 | scan-complete provider, 748,888 B `b42ff4ec...` (local alt 748,564 B `02162d4a...`) | `allow_frames=16 vap_start_first=1 vap_start_mode=1 vap_start_module=0 frame_gap_ms=100 h2d_send_budget=8 scan_timeout_ms=60000` | chans=13/13 + finish-ok + results/bss>0 + iw BSS lines + mask/staging 0 + page/state golden + provider refused/timeout/bad=0 |
| **A** AP start | cfg80211 | wifi-ap-start provider, 770,304 B `11511980...` | `allow_frames=16 vap_start_first=1 vap_start_mode=1 vap_start_module=0 frame_gap_ms=100 h2d_send_budget=8 ap_start_mode=0` | base PASS + iface type __ap + VAP-base frames + the carrier's ap_* frames (g0 mode 0: ap_ssid 0x0005 64 B -> ap_cfg 0x0127 184 B; WAL g2 mode 1: 4 frames) + hostapd rc=0 + mask/staging 0 + page/state golden + VAP readback (100/2 or PASS-PARTIAL) |
| **X** first TX/RX | cfg80211 | merged provider+dp, 884,576 B `ec74fccd...` (canonical build /tmp/wifi-dp-air-build) | from the F-AIR sequence: `allow_frames=12 frame_gap_ms=100 h2d_send_budget=4 vap_start_first=1 vap_start_mode=1 vap_start_module=0` | F-AIR ladder: ctx_create -> txq -> iface+ONE scan (mode-1 VAP + channel) -> dp_probe -> dp_setup -> 0x0320 -> TX posts -> completion read -> read-only RX capture -> packs; COMPLETED / POST-ONLY / NO-RX / BLOCKED-DRIVER, never a fabricated air claim |

## 5. Artifact pins and the two external prerequisites

`artifacts.env` pins every byte the session deploys: PD4 firmware
928,920 B `cfe24bfb...` (+ its manifest), the 2,472 B PHY block
`0b4ef918...`, cfg80211.ko 2,328,548 B `949393f9...`, iw
`6345e1ca...`, libnl-tiny `59789910...`, the three modules above. Two
prerequisites come from the parallel lanes and are wired as pins/contracts:

1. **hostapd (wifi-hostapd-deploy, task-125)** - canonical bench path is that
   lane's **T0 zero-flash apk-add** of the four packages (hostapd-mini +
   hostapd-common + ucode-mod-nl80211 + ucode-mod-rtnl; every other shared lib
   is already in the bench image). Run it attended before this session:
   `cd ../wifi-hostapd-deploy && ./deploy-hostapd.sh --apply --attended --yes-really`.
   (lane deliverable: [../wifi-hostapd-deploy/AP-START-LADDER.md](../wifi-hostapd-deploy/AP-START-LADDER.md).)
   The package pins the *on-box* artifacts from that lane's manifest:
   `/usr/sbin/hostapd` sha256 `13cb3a43...` and
   `/root/hostapd-deploy/hostapd-open.conf` sha256 `e4f6edc6...`; phase A
   verifies both over ssh before firing and refuses on a mismatch. The
   `HOSTAPD_MODE=static` fallback (push one binary to /root) remains in the
   driver for emergencies.
2. **merged provider+dp module + F-AIR ladder (wifi-dp-air, task-126)** - the
   module (884,576 B `ec74fccd...`) and
   `../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json` (sha256
   `96354365...`) are committed; phase X runs the same bench_session
   invocation shape as S/A. The lane's bench_guards payload policy for the
   dp_tx_frame/dp_rx_poll stores is the last host-side item in flight; while it
   is missing, the X plan is refused (fail-closed, no bench time burnt).

A still-unfilled pin makes its phase `DEPENDENCY-PENDING`: a real run refuses
it (whole session refused unless `--allow-partial`); a dry-run still plans and
renders every phase whose own sequence is complete.

## 6. Evidence

`build/wifi-bench-session/<UTC stamp>/` (gitignored; /tmp is never evidence):

| path | content |
| --- | --- |
| `s/` | bench_session evidence: session-plan.json, device-script.sh, report.txt (all device READ lines + dmesg), summary.json, protocol-evidence.json, PROTOCOL-SEQUENCE.md, writes-host.jsonl |
| `a/` | the same for the base session + `ap-fire-report.txt`, `ap-readback-report.txt`, `ap-fire.log`, `ap-readback.log` |
| `x/` | the dp-merge ladder evidence dir (same layout) |
| `SESSION-REPORT.md`, `session.json`, `cleanup-reboot.log` | driver report, machine record, final reboot result |

Copy the useful lines (golden page, scan model line, frame-post lines, dmesg
verdicts) into the ledger/EVIDENCE-INDEX when the Lead writes the session
entry.

## 7. Relationship to the committed bring-up tooling

This lane does not fork the runner: phases S/A/X are **data** (sequence JSON)
plus a thin driver around the committed, hardware-validated
`wifi-bench-tooling/bench_session.py` (script generation, static audit,
fail-closed gates, hardened reboot). The only new device code is phase A's
hostapd stage (`device/ap-fire.sh` + readback), which is read-only for
firmware state and uses nl80211/hostapd for its two device actions. The exact
generated device scripts are committed under `generated/` for review.

Related: [../wifi-scan-complete/](../wifi-scan-complete/) (module + scan
window), [../wifi-ap-start/](../wifi-ap-start/) (AP frames + ladder),
[../wifi-hostapd-deploy/](../wifi-hostapd-deploy/) (hostapd build + T0
zero-flash deploy + AP-START-LADDER), [../wifi-dp-air/](../wifi-dp-air/)
(merged provider+dp module + LADDER-F-AIR), [../wifi-dp-txrx/](../wifi-dp-txrx/)
(TX/RX implementation), [../wifi-image-deploy/](../wifi-image-deploy/) (T0
deploy mechanism), [../wifi-bench-tooling/](../wifi-bench-tooling/) (runner +
guards; bringup-f-air-txrx.json).