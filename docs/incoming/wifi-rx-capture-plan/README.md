# wifi-rx-capture-plan - the decisive MAC-RX capture experiment (task-165)

**Lane:** rx-capture-plan. **Host-only design lane**: the Lead fires, this lane
never contacts the bench router.

## The problem this lane addresses

The AP is up and the client provably transmits 802.11 authentication frames at
the AP BSSID, but the router's MAC RX path produces nothing: no management RX
event (D2H g1 id13), the live host-MAC bank never moves, hostapd sees nothing.
E-RX-1 captures every host-visible RX channel before/during/after that proven
transmission with a READ-ONLY router instrument, and the classifier maps the
observations to one of four mechanically different failure hypotheses
(MAC/PHY never received; MAC saw it but could not write it; firmware received it
but did not deliver the event; exception contamination) plus the inconclusive /
no-trigger cases. It also includes crash detection during the capture because
the same session showed a firmware exception (mask 0x04000010) after extended
RX drain loops.

## What is here

| path | what it is |
| --- | --- |
| [REPORT.md](REPORT.md) | the experiment design: hypotheses, evidence base, exact read matrix and addresses, the new host-MAC interrupt status bit decode, crash-trigger analysis + isolation ladder, analysis criteria/decision tree, limits |
| [RUNBOOK.md](RUNBOOK.md) | the attended procedure: preconditions, phase 0b air discriminator, the one-command capture run, crash handling, stop rules, evidence naming |
| [ANALYSIS-TEMPLATE.md](ANALYSIS-TEMPLATE.md) | the fill-in worksheet for the bundle |
| [scripts/router/rx-capture-router.sh](scripts/router/rx-capture-router.sh) | READ-ONLY router instrument: check, snapshot (sysfs + live bank + config block + FW exception state + RX ring memory + per-buffer header sweep), timestamped sampler with crash watch, collect |
| [scripts/host/rx-capture-host.sh](scripts/host/rx-capture-host.sh) | host orchestrator: preflight, push, check, pre snapshot, sampler, client auth trigger, post snapshot, pull, analyze |
| [scripts/host/analyze_capture.py](scripts/host/analyze_capture.py) | bundle classifier (8 verdicts) + synthetic-fixture selftest |
| [evidence/](evidence/) | committed host-test output (no hardware claims) |

## Quick start (host-only checks)

    cd opensource/docs/incoming/wifi-rx-capture-plan
    sh scripts/router/rx-capture-router.sh selftest        # 13 checks
    python3 scripts/host/analyze_capture.py selftest       # 8/8 verdict fixtures
    sh scripts/host/rx-capture-host.sh selftest            # script presence + analyzer

## Attended run (the Lead only)

    scripts/host/rx-capture-host.sh preflight
    scripts/host/rx-capture-host.sh run --label rx1-<date> --attempts 4 --seconds 90 --hz 5

Exit 0 = verdict produced; 3 = firmware exception during/around the capture
(V-CRASH); 2 = preflight/check refused. Evidence stays under
build/wifi-rx-capture/<label>/ (gitignored). Read the four facts in REPORT.md
section 9 (limits) before claiming a result.

## Safety

* The router script is READ-ONLY by construction: no device register write, no
  /dev/mem write, no module load/unload, no parameter change, no reboot. Its
  selftest proves the absence of those forms. The only optional device action
  (--allow-oneshot) writes "1" to the documented read-only one-shot
  dp_rx_arm/dp_rx_poll triggers and is off by default.
* E-RX-1 runs without dp_rx_drain, so it never moves an RX ring pointer.
* Never write CA 0x400392f0; never read the RC misc window 0x10161000; never
  touch flash/selector in this procedure.
* No vendor binaries, firmware bytes, calibration data or secrets are in this
  lane; the address/decode facts are derived from committed driver sources and
  the lane reports cited in REPORT.md.

