# generated

The **exact device-side shell scripts** for phases S, A and X, snapshotted from
`bench_session.py --dry-run` (the committed, hardware-validated runner).  They
are what the host driver executes on the box at fire time:

| file | phase | generated from |
| --- | --- | --- |
| `phase-s-device-script.sh` | S - 13-channel scan | sequences/session-scan13.json |
| `phase-a-device-script.sh` | A - AP base ladder | sequences/session-ap-base.json |
| `phase-x-device-script.sh` | X - first MAC TX/RX | ../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json |

They are **review snapshots, not the execution path**: every dry-run/real run
re-renders the current script into its evidence dir
(`build/wifi-bench-session/<stamp>/<phase>/device-script.sh`) and statically
audits it (no CA 0x400392F0 / RC-misc / forbidden attrs; devmem only inside the
custom-block windows).  If a sequence changes, re-run
`./host/run-session.sh` and copy the new script here (the session id in the
header is the dry-run stamp, which is expected to differ).

Phase A also has the hand-written device stage
[`../device/ap-fire.sh`](../device/ap-fire.sh) (hostapd + AP-frame gates +
firmware VAP readback) and [`../device/ap-readback.sh`](../device/ap-readback.sh);
those are executed after the A-base script.
