# AP soak result - date / operator / image

Copy this template into results/soak-DATE.md after a run (text only).

## Header

- verdict: **PASS | FAIL-...**
- date/time (UTC):
- image: container sha256 / kernel slice sha256 / rootfs sha256:
- driver module: size / sha256 (+ stop_ap_mode if used)
- firmware: PD4 sha256 cfe24bfb... (staged by hand, never packaged)
- duration requested / actual (s):
- interval (s) / samples:
- operator / recovery door armed: yes

## Analyzer result

(Paste the table produced by tools/soak.py analyze / run.)

| check | status | detail |
| --- | --- | --- |
| boot-page | | |
| no-reboot | | |
| dmesg | | |
| excp-info | | |
| heartbeat | | |
| d2h | | |
| ap-liveness | | |
| hcc-phase | | |
| memory | | |
| duration / coverage / gaps | | |

## Metrics

- heartbeat: min rate (1/s) / max zero run (s) / baseline ~1.01 s
- D2H: frames total / overruns / drop
- hostapd: procs / RSS first-last (KB)
- MemAvailable first-last (KB) / drift (KB)
- dmesg anomaly lines (first):
- evidence directory (local, gitignored):
