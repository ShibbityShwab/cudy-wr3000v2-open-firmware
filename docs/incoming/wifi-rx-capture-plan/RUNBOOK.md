# RUNBOOK - E-RX-1 attended RX-capture procedure

**Lane:** wifi-rx-capture-plan (task-165). The Lead fires. The router side is
READ-ONLY; the only device writes in the whole session are the normal
module-load/boot/AP-start/dp_setup/arm sequence that the session already has.

## R0. Roles and preconditions

| item | required state |
| --- | --- |
| bench | human at the box, recovery door armed per BENCH-FACTS/RUNBOOK; one Wi-Fi power-cycle available |
| router | our image, hi5622_wifi loaded, AP-ENABLED (hostapd), page golden, mask 0, staging 0 |
| session order | AP start FIRST -> dp_probe -> dp_setup -> h2d_txq_addr_sync -> dp_rx_arm (read-only). NO drain loop |
| module params | rx_enable=1, rx_refill_wm as baked, mgmt_rx=1 (for delivery evidence), mgmt_tx=1 |
| client | workstation Wi-Fi free (NetworkManager will be touched by the harness reset), sudo available |
| host evidence | build/wifi-rx-capture/<label>/ (gitignored) |

A drain loop must be stopped before E-RX-1: run the session's own drain-stop,
then confirm with the router check that no drain process is running. E-RX-1
never starts one.

## R1. Host preflight (no device contact)

    cd <repo root>
    opensource/docs/incoming/wifi-rx-capture-plan/scripts/host/rx-capture-host.sh preflight

PASS requires: rsh/rscp present, router script + analyzer present and
self-tested, python3, iw/ip, and passwordless sudo (the client harness needs
root). If sudo is not passwordless, run the runbook manually as root or
pre-authorize sudo -n.

## R2. Router check (read-only)

    opensource/docs/incoming/wifi-rx-capture-plan/scripts/host/rx-capture-host.sh run --skip-client --seconds 5 --label <label>-check

or, manually on the router:

    sh /tmp/rx-capture-router.sh check

The check verifies: module loaded, rx_enable=1, the attribute set of the
d2c608e7-class module, raw /dev/mem availability (golden-page read self-test),
the current page/mask/staging, and whether a drain loop is running. CHECK PASS
is required. The check output is saved by the orchestrator in router-check.txt.

## R3. Phase 0b - optional host-only air discriminator

Before or after E-RX-1, with no router login:

1. create a monitor interface on the client NIC (iw phy <phy> interface add mon0
   type monitor; iw dev mon0 set channel 6), if the driver allows it next to the
   managed interface;
2. start a capture filtered to the AP BSSID 06:05:04:03:02:01;
3. run one auth attempt (harness assoc);
4. inspect: client auth frame present? AP ACK (control subtype 0x1d) present
   after it? any probe response (subtype 0x05) to the client's probe request?

If the AP ACKs the client frame, radio-level RX works and the bug is in the
firmware/host path (H1 excluded at radio level). If no ACK appears, the RX
chain/channel/mode is the first suspect. If monitor+managed concurrency is not
supported, record UNAVAILABLE and continue with E-RX-1.

## R4. E-RX-1 capture + trigger (the decisive run)

    cd <repo root>
    opensource/docs/incoming/wifi-rx-capture-plan/scripts/host/rx-capture-host.sh run \
        --label rx1-<date> --attempts 4 --seconds 90 --hz 5

Sequence performed by the orchestrator:

1. push the router script to /tmp/rx-capture-router.sh, run check (abort on fail);
2. router pre snapshot: sysfs evidence, live bank, config block, firmware
   exception state, RX rings, per-buffer header sweep, dmesg, iw state;
3. start the router sampler (read-only; 5 Hz; crash watch) with a 90 s budget;
4. client: reset -> scan -> 4 auth attempts, saving the harness output and the
   client dmesg send-auth lines; marker epochs are written to host-markers.log;
5. stop the sampler; router post snapshot (same channels);
6. collect + pull the bundle; run the classifier; print the verdict.

The sampler also stops early and writes crash-first.txt if page/mask/staging or
excp_announce deviates. A crash makes the run exit 3 (V-CRASH); do not repeat
E-RX-1 in the same module load - power-cycle the Wi-Fi chip and move to R5.

## R5. If the crash appears (issue B)

1. stop; power-cycle the box (SoC reboot does NOT reset the Wi-Fi chip);
2. record the crash-first.txt window and the last drain/action line;
3. run the isolation ladder from REPORT.md section 6.1 (B0 arm-idle, B2 one
   drain with rx_refill_wm=0, B3 drain loop wm=0, B4 wm=75, B5
   rx_clear_status=1), one rung per fresh power-on, crash watch on each;
4. the first rung that traps names the write family; hand it to the patch lane
   with the exact dmesg window and the B-rung number.

## R6. Reading the result

Open analysis.txt in the evidence directory. The classifier prints:

* the verdict (V-...) and the reason;
* every evidence channel with its value (live bank pre/post, status bits,
  completion ring non-zero count, header-change count, D2H delta, dmesg
  delivery/exception lines, client attempts);
* the next action for that verdict.

Fill ANALYSIS-TEMPLATE.md with the same bundle before the next bench session.
Do not over-read the null: a management-only trigger is expected to leave the
data rings at zero (REPORT.md section 3).

## R7. Stop rules (hard)

* Any page/mask/staging deviation, excp_announce != 0, or crash-first.txt:
  STOP, power-cycle; the run is V-CRASH.
* Any raw read of 0xffffffff (transport dead): STOP; do not retry in the load.
* Any unexpected write to the router: the router script is read-only; if you
  see one, stop and report it as a tooling bug.
* Never rmmod/insmod/reload to "retry" E-RX-1; one load, one idea.
* Never write CA 0x400392f0, never read the RC misc window 0x10161000, never
  touch flash or the selector from this procedure.

## R8. Evidence naming

    build/wifi-rx-capture/<label>/
      router-check.txt             read-only preflight
      router-pre.txt               pre snapshot console
      router-sampler-start.txt     sampler start line + pid
      router-sampler-stop.txt      sampler stop line
      router-post.txt              post snapshot console
      router-collect.txt           bundle path + md5
      router-bundle.tar.gz         raw bundle (snap-<label>-pre/-post/sampler)
      client-reset.txt             harness reset log
      client-scan.txt              harness scan log (AP visible?)
      client-assoc-<n>.txt         harness auth attempt log (AUTH-TIMEOUT expected)
      client-dmesg-auth.txt        client-side send-auth proof
      host-markers.log             attempt windows (epoch)
      analysis.txt                 classifier output + verdict

The router-side tree /tmp/rx-capture is volatile; the bundle is the evidence.

